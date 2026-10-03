# Ajouter un format

L'architecture reprend le principe de CyImgView : l'application ne connaît pas les bibliothèques de formats.
Elle lit les petits manifestes au démarrage, puis charge une DLL / `.so` / `.dylib` à la première ouverture d'un format.

Un plugin fournit deux fichiers dans `plugins/`, à côté de l'exécutable :

```text
my_format.dll
my_format.dll.cy3d
```

Le manifeste UTF-8 contient quatre lignes :

```text
Mon format
my_format.dll
foo;bar
10
```

La priorité est facultative (0 par défaut). Le plugin de priorité la plus élevée est choisi pour une extension.
Le nom de bibliothèque doit désigner un fichier du même dossier. Priorités : passerelles 200, points 150, STL 100, Assimp 0.
Le code -3 décline uniquement un sous-ensemble non pris en charge (ex. PLY avec faces dans le lecteur de points) ;
l'hôte essaie alors le prochain plugin de la même extension. Une erreur réelle ou une annulation ne déclenche pas de repli.

## ABI C version 3

Voir `include/cy3d_plugin.h`, le lecteur STL indépendant dans `plugins/stl/importer.cpp`,
et l'adaptateur Assimp dans `plugins/assimp/importer.cpp`.

Exporter `Cy3D_GetPlugin`, retournant un `Cy3DPlugin` statique avec `api_version` et `struct_size` corrects.
Les appels `load` peuvent être concurrents : créer un état indépendant par import, sans état global mutable.
Les callbacks de progression et d'annulation peuvent être appelés depuis le thread d'import, jamais depuis un thread persistant après son retour.
Aucune exception ne doit traverser la frontière C. Retourner 0 si l'import réussit, -1 si une erreur survient,
-2 si annulé, -3 pour un sous-ensemble non pris en charge. Un échec ne doit pas produire de scène.
Initialiser `*output` à NULL avant de commencer ; toute allocation partielle reste à libérer par le plugin lors d'un échec.

Le plugin conserve ses allocations jusqu'à `release`. L'hôte appelle la fonction `release` du même plugin ;
aucun objet STL, allocateur C++, objet graphique ou runtime ne traverse l'ABI.
La bibliothèque reste chargée tant qu'une scène issue du plugin existe.

- Chemins et chaînes en UTF-8 sur les trois plateformes.
- Sommets intercalés : position XYZ, normale XYZ, UV0 XY, UV1 XY (40 octets).
  Si UV1 est absent, recopier UV0. Indices `uint32_t`, topologie triangles, points ou splats.
- Chaque maillage n'est stocké qu'une fois ; les instances portent des matrices colonne-major 4×4.
- Normales et UV en espace local ; Y vertical ; les adaptateurs convertissent les conventions de leurs formats.
- Les bounds sont en espace monde, finies, avant tout cadrage de la caméra.
- Chaque maillage indique aussi son centre en espace local pour trier les instances transparentes.
- Initialiser les matériaux avec `Cy3D_DefaultMaterial()`, puis remplir les propriétés disponibles.
  Une structure uniquement remplie de zéros ne constitue pas un matériau valide par défaut.
- Couleur de base RGBA et émission RGB en **linéaire** ; facteurs métal/rugosité,
  intensité des normales et occlusion, seuil/mode alpha, double face et unlit.
- Six `Cy3DMap` : couleur, métal, rugosité, normale, occlusion, émission. `texture=-1` signifie aucune image.
  Les slots couleur/émission sont des images sRGB ; les autres sont des données linéaires.
  Un slot scalaire utilise `channel` (0=R, 1=G, 2=B, 3=A). Le glTF ORM utilise R=AO, G=roughness, B=metal.
- `uv_set` vaut 0 ou 1 ; `transform` est une matrice 3×3 colonne-major appliquée aux UV du sommet
  **avant** la conversion V -> 1-V pour l'image. `wrap_u`/`wrap_v` sélectionnent repeat, clamp ou mirror.
  Les normal maps suivent la convention tangentielle +Y de glTF ; leur base est reconstruite par dérivées.
- Les images peuvent être partagées par plusieurs slots. L'hôte ne les décode qu'une fois.
  Une image utilisée à la fois comme couleur et comme données nécessite deux représentations GPU,
  pour conserver des filtrages et mipmaps corrects dans les deux espaces colorimétriques.
- Les textures sont des fichiers externes absolus, des octets encodés, ou du RGBA8 embarqué.
- Fournir les dépendances externes ouvertes (ex. MTL, BIN) pour invalider le cache lorsqu'elles changent.
  L'hôte ajoute automatiquement le modèle et les textures externes à cette liste.
- `memory_bytes` estime la mémoire détenue par la scène. Respecter `max_output_bytes`,
  vérifier l'annulation régulièrement, et fournir un message d'erreur terminé par NUL.
- `colors` est un tableau optionnel de RGBA **linéaire**, quatre floats par sommet ; il multiplie le matériau.
- `skin` est optionnel et possède quatre indices/poids par sommet, vers les os du maillage.
  Normaliser les poids ; un sommet sans influence utilise sa transformation d'instance.
  Chaque os désigne un nœud et une matrice inverse de liaison ; les palettes GPU sont `world(node) * inverse_bind`.
- Un splat exige `splats` (échelles positives XYZ, quaternion normalisé XYZW) et `colors`.
  La topologie splat est rendue en transparence et triée par profondeur ; le maillage doit fournir les centres/indices.
- `Cy3DInstance.node=-1` indique une transformation statique. Pour une instance animée, fournir l'indice
  de son nœud ; sa matrice initiale représente la pose mondiale importée.
- `Cy3DNode.parent=-1` indique une racine, sinon le parent doit précéder l'enfant dans le tableau.
  `transform` est la matrice locale de liaison ; translation, rotation XYZW et échelle sont sa décomposition TRS.
  `bone` indique les nœuds visibles dans la superposition du squelette.
- Les clips durent `duration` secondes. Une piste désigne un nœud et des clés de position/rotation/échelle.
  Les temps sont finis et triés. `step=1` conserve la valeur jusqu'à la clé suivante ; sinon interpolation
  linéaire, ou sphérique pour les quaternions. Les composantes inutilisées de `value[4]` doivent être initialisées.
- Fournir `warnings` pour les fonctions importées partiellement. L'hôte copie clips, nœuds et messages
  avant le transfert GPU ; le rendu garde les animations même si la géométrie CPU quitte son cache.

Compilation minimale :

```cmake
add_library(my_format MODULE importer.cpp)
target_include_directories(my_format PRIVATE /chemin/vers/Cy3DView/include)
set_target_properties(my_format PROPERTIES PREFIX "")
```

Pour produire un manifeste à partir des extensions déclarées par un plugin :

```text
cy3d_probe --manifest /chemin/absolu/my_format.dll 10
```

Les plugins sont du code natif chargé dans le processus. Utiliser des modules de confiance ; cette ABI n'offre pas de sandbox.

### Migration depuis v1

La version 0.3 modifie la taille des sommets, maillages et matériaux : **recompiler les plugins avec l'en-tête v2**.
L'hôte rejette les modules v1 avec un message de version incompatible, avant d'appeler leur importeur.
Remplacer l'ancien champ `material.texture` par `material.maps[CY3D_BASE_COLOR].texture`,
initialiser les autres slots à -1 grâce au constructeur de valeurs par défaut, et fournir UV1 et le centre du maillage.

### Migration depuis v2

La version 0.4 exige de recompiler avec l'en-tête **v3**. Initialiser tous les nouveaux champs à zéro,
`topology=CY3D_TRIANGLES`, et **`instance.node=-1`** pour les scènes statiques.
`animation_count` doit maintenant correspondre aux clips réellement fournis ; ne plus indiquer seulement leur présence.
Les sommets statiques gardent leur taille de 40 octets ; couleurs, skin et splats restent des tableaux optionnels séparés.

## Export et outils (0.6)

L'ABI d'import v3 reste inchangée. L'ABI d'extension v1 permet de déclarer des
exporteurs et des outils avec leur panneau de réglages : voir
[PLUGIN_EXTENSIONS.md](PLUGIN_EXTENSIONS.md), `include/cy3d_extension.h`,
`plugins/export/exporter.cpp` et l'éditeur `plugins/xatlas/tool.cpp`.
