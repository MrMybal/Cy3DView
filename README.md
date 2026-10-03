# Cy3DView

**Cy3DView by Cyberalien · GNU GPL v3.0**

Visionneuse de modèles 3D inspirée de **CyImgView** : C++ natif, rendu GPU à la demande,
chargement en arrière-plan, cache borné et formats extensibles par plugins.

## Licence

Cy3DView est distribué sous **GNU GPL v3.0 uniquement** (`GPL-3.0-only`).
Voir [LICENSE](LICENSE) et [NOTICE](NOTICE). Le logiciel est fourni sans garantie.
Les bibliothèques tierces conservent leurs licences et attributions : voir
[docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).

Dépôt du projet : [MrMybal/Cy3DView](https://github.com/MrMybal/Cy3DView).

La version 0.5 possède un moteur portable Windows / Linux / macOS (OpenGL 3.3).
La compilation et le rendu ont été vérifiés sur Windows ; les configurations Linux/macOS sont prévues dans la CI.
CyImgView lui-même est actuellement une application Windows : Cy3DView en reprend les principes,
pas le code Win32/Direct2D.

## Lancer

Sous Windows, télécharger `Cy3DView-<version>-win64-setup.exe` depuis les
[versions GitHub](https://github.com/MrMybal/Cy3DView/releases). L'installeur propose
anglais/français et installe dans `%LOCALAPPDATA%\Programs\Cy3DView`, sans droits administrateur.
Il ajoute le menu Démarrer, un raccourci bureau facultatif et l'entrée de désinstallation Windows.
La version ZIP portable reste disponible.

Le bouton **Updates / Mises à jour** recherche les versions stables, affiche leurs notes,
télécharge l'installeur et vérifie sa taille et son empreinte SHA-256 publiée par GitHub.
**Close app and install / Fermer l'application et installer** ouvre l'assistant dans le dossier
actuel après fermeture de l'application. Les préférences et plugins supplémentaires sont conservés.
L'assistant permet de relancer l'application après l'installation. La recherche est manuelle ;
l'application n'effectue aucune requête de mise à jour au démarrage.

Pour un dépôt public, aucun compte n'est requis. Tant que le dépôt est privé, l'accès facultatif
demande un jeton GitHub disposant de **Contents: read** pour ce dépôt ; il reste uniquement en mémoire
pendant la session. Le bouton **GitHub releases / Versions GitHub** permet aussi d'ouvrir le navigateur.
Sous Linux/macOS, la recherche est disponible et le paquet s'installe manuellement depuis cette page.

Après compilation : `build/Release/bin/Cy3DView.exe`, ou l'exécutable `Cy3DView` sous Linux/macOS.
On peut ouvrir un fichier par glisser-déposer, par l'explorateur intégré, par **Ouvrir** sous Windows,
ou en le passant sur la ligne de commande. Les démonstrations incluses contiennent un nœud torique turquoise
et `samples/pbr_studio.gltf` : 18 sphères, métal de haut en bas et rugosité de gauche à droite.
`animated_flag.glb`, `colored_cloud.ply`, `gaussian_cloud.splat` et `usd_scene.usda` illustrent les nouveaux lecteurs.

Le dossier `plugins/` doit rester à côté de l'exécutable. L'application fonctionne hors ligne.
L'interface démarre en **anglais**. Le sélecteur **English / Français** dans la barre supérieure
change immédiatement la langue. Le choix est conservé dans `Cy3DView.ini`, à côté de l'exécutable
(le dossier doit être accessible en écriture pour mémoriser cette préférence).
La ligne de commande accepte également `--language en` ou `--language fr`.
Le logo violet est embarqué dans l'application : en-tête, accueil, fenêtre des plugins et icône Windows.
Pour remplacer le visuel dans les sources, mettre à jour `cy3dview-logo-violet.png`, puis exécuter
`python tools/prepare_logo.py` (Pillow requis pour cette préparation seulement) et recompiler.

## Fonctions disponibles

- Affichage des triangles, normales, couleurs de matériaux et textures de couleur externes ou embarquées.
- Rendu **PBR metallic/roughness** : BRDF GGX, Fresnel, éclairage direct et studio HDR intégré.
- Reflets de studio préfiltrés selon la rugosité, lumière diffuse ambiante et table d'intégration BRDF.
- Textures de couleur, métal, rugosité, normales, occlusion et émission ; images partagées dédupliquées.
- Textures de couleur/émission en sRGB, données de matériau en linéaire, exposition et tone mapping.
- Import glTF des canaux ORM, UV0/UV1, transformations UV, répétition/clamp/miroir,
  intensité des normales, occlusion, émission, matériaux unlit et modes alpha opaque/mask/blend.
- Réglages d'exposition, intensité et orientation du studio ; relief et occlusion désactivables.

Le mapping des matériaux suit les conventions de la [spécification glTF 2.0 de Khronos](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).

Autres fonctions :

- Animations de transformations et de squelettes, déformation des sommets sur le GPU,
  sélection du clip, lecture/pause, boucle, vitesse et déplacement sur la timeline.
- Interpolation linéaire et STEP, rotations par interpolation sphérique, affichage des os.
- Couleurs de sommets, nuages de points avec taille réglable et splats gaussiens anisotropes.
- Transformations de scène et instances : un maillage partagé n'est importé/enregistré sur le GPU qu'une fois.
- Caméra orbitale, déplacement continu avec **WASD ou ZQSD**, zoom, cadrage automatique et plein écran.
- Interface anglais/français, anglais par défaut, changement de langue immédiat et préférence mémorisée.
- Modes matériaux, argile, normales et filaire ; grille désactivable et rotation automatique.
- Explorateur de dossier avec tri naturel, filtre de recherche et navigation entre modèles.
- Chargement sur un thread dédié avec annulation des demandes obsolètes.
- Bandeau de chargement animé, étape en cours, temps écoulé et pourcentage pendant la préparation GPU.
- Transfert progressif des sommets, indices et textures vers le GPU ; le modèle précédent reste visible jusqu'à la fin.
- Annulation explicite avec Échap ou le bouton Annuler, y compris pendant le transfert GPU.
- Cache LRU de scènes de **256 Mio**, invalidé lorsque le modèle ou ses dépendances changent.
- Cache GPU LRU de **256 Mio estimés** : les buffers d'une scène inchangée sont réutilisés quand on y revient.
- Préchargement des deux voisins, y compris après la lecture du dossier d'un fichier ouvert au démarrage.
- Décompression des textures sur le thread d'import, budget de **256 Mio** de RGBA par scène.
- Rendu uniquement lorsqu'un événement arrive, hors déplacement clavier, chargement, saisie, animation ou rotation automatique.
- Informations de géométrie, dimensions, temps d'import, temps d'envoi GPU et mémoire estimée.
- Lignes de l'explorateur dessinées seulement lorsqu'elles sont visibles, avec filtre et navigation indexés.
- Captures PNG du viewport, avec confirmation avant de remplacer un fichier existant.

| Raccourci / geste | Action |
|---|---|
| Ctrl+O | Ouvrir (sélecteur Windows ; explorateur intégré ailleurs) |
| Glisser gauche | Orbite |
| Glisser droit / milieu | Déplacer la caméra |
| WASD / ZQSD | Avancer, reculer et se déplacer latéralement selon la direction de la caméra |
| Maj + WASD / ZQSD | Déplacement quatre fois plus rapide |
| Molette | Zoom |
| Home / double clic | Recentrer |
| Gauche / droite | Modèle précédent / suivant |
| F / G / T | Filaire / grille / textures |
| Espace | Rotation automatique |
| F11 / Échap | Plein écran / annuler le chargement ou quitter le plein écran |
| Ctrl+S | Capture PNG |

Les deux dispositions clavier fonctionnent simultanément : W ou Z pour avancer, A ou Q pour aller
à gauche, S pour reculer et D pour aller à droite. La vitesse dépend de l'échelle du modèle et du zoom ;
elle reste identique en diagonale et indépendante de la fréquence d'affichage. Les déplacements sont
bloqués pendant le chargement, la saisie de texte, l'utilisation d'un contrôle, l'ouverture d'un dialogue
ou lorsque la fenêtre perd le focus. Relâcher les touches remet le rendu au repos.

## Formats

**STL natif** : lecteur dédié ASCII et binaire, fichier mappé en mémoire, sans copie de l'entrée,
avec annulation et contrôle des tailles. Les sommets par face conservent les arêtes nettes ; ce lecteur
privilégie la vitesse et utilise davantage de mémoire que le soudage des sommets d'Assimp.

**Assimp 6.0.2** : OBJ/MTL, FBX, glTF/GLB, PLY, Collada/DAE, 3DS, 3MF, OFF, X, LWO,
DXF, IFC et d'autres extensions annoncées par la bibliothèque. La liste exacte est générée
depuis le plugin compilé, accessible avec le bouton **Plugins** ou `cy3d_probe --formats`.
Une extension annoncée ne garantit pas la compatibilité avec toutes les versions/variantes du format.

**Nuages natifs** : PLY ASCII, binaire little/big endian, XYZ et PTS avec positions et couleurs.
Les PLY comportant des faces sont confiés à Assimp. Pour XYZ/PTS : XYZ, XYZ RGB ou XYZ intensité RGB,
couleurs RGB de 0 à 255 ; PTS commence par le nombre de points. Les propriétés PLY peuvent être réordonnées ;
les couleurs PLY flottantes sont interprétées de 0 à 1.

**Gaussian Splatting** : `.splat` de 32 octets par point (positions, échelles, RGBA et quaternion WXYZ),
et PLY gaussian avec `f_dc_*`, `opacity`, `scale_*`, `rot_*`.
Projection de la covariance anisotrope, transparence gaussienne et tri en profondeur par radix.
Les coefficients SH directionnels au-delà du degré 0 restent ignorés ; pas encore de `.ksplat`, `.spz`
ou de streaming de scènes dépassant la mémoire disponible.

**USD / USDA / USDC / USDZ** : lecteur natif expérimental via Assimp et tinyusdz épinglés.
Les quatre conteneurs sont testés, dont un USDC produit avec Blender 5.2.
Ce lecteur ne garantit pas toutes les compositions, variantes, matériaux et animations USD.
Les fichiers USD sont relus à chaque ouverture, car le lecteur amont n'expose pas toutes ses dépendances
externes à notre cache. Préférer des scènes autonomes/aplaties pour cette première intégration.

**Blender récent** : plugin de conversion automatique utilisant un Blender installé, puis notre lecteur GLB.
Vérifié avec Blender 5.2.1, animations et skin compris. Les scripts intégrés au fichier sont désactivés.
Blender n'est pas embarqué ; `CY3D_BLENDER` peut indiquer le chemin de son exécutable.
Les matériaux procéduraux non exportables en glTF ne sont pas cuits automatiquement.
Les références de textures exportées avec un indice UV invalide sont corrigées : le jeu UV principal
est utilisé lorsqu'il existe ; sinon les valeurs PBR constantes du matériau Blender remplacent ces textures,
avec un avertissement. La conversion ne modifie pas le fichier `.blend` source.

**Unreal `.uasset`** : passerelle optionnelle utilisant l'Unreal Editor correspondant au projet.
Vérifiée sous Windows avec UE 5.6 et des StaticMesh dans `/Engine/` et `/Game/`.
Le fichier doit conserver son emplacement dans `Content`, son `.uproject` et ses dépendances.
Seuls StaticMesh/SkeletalMesh non cuits sont acceptés ; pas les packages de jeux, Blueprints, mondes,
ni les classes natives de projet ou les montages de plugins spécifiques.
`CY3D_UNREAL_EDITOR` permet de choisir l'exécutable `UnrealEditor-Cmd`.
Les assets source ne sont pas sauvegardés. La cuisson des matériaux est désactivée pour éviter de compiler
des shaders ; les expressions exportables en glTF sont reprises, les autres peuvent être simplifiées.
Les clips AnimationSequence distincts ne sont pas associés automatiquement à un SkeletalMesh.

Ces deux passerelles lancent une conversion en arrière-plan au premier accès ; elles peuvent prendre
plusieurs secondes. Les GLB convertis sont mis en cache dans le dossier temporaire `Cy3DView-conversions`,
avec un budget de 256 Mio et invalidation sur changement des fichiers, dépendances, outils et scripts.
Le chargement peut être annulé ; les processus de conversion sont limités à quatre minutes.
La détection d'Unreal est actuellement Windows ; ailleurs, utiliser la variable d'environnement.

Les formats CAD propriétaires et géométries compressées Draco ne sont pas inclus dans ce build.
STEP/STP annoncé par Assimp correspond à ses importeurs spécifiques, pas à un moteur CAD général.

Les morph targets, contraintes, simulations et courbes CUBICSPLINE exactes restent à ajouter.
Pour ces dernières, Assimp ne conserve pas les tangentes : une interpolation linéaire/sphérique est utilisée
avec un avertissement. Au maximum quatre influences d'os sont conservées par sommet.
Le cadrage initial utilise les dimensions de la pose importée ; un mouvement très ample peut demander un zoom arrière.
Le PBR couvre le workflow metallic/roughness ; clearcoat, transmission, sheen, anisotropie
et specular/glossiness ne sont pas interprétés fidèlement. Pas encore d'import d'environnement HDR personnel,
d'ombres portées, de SSAO ou d'anticrénelage. L'occlusion vient des textures du matériau.
Les transparences sont triées par centre d'instance, sans tri des triangles : des surfaces qui s'entrecroisent
peuvent être incorrectes. Les normal maps utilisent une base tangente reconstruite à partir des dérivées UV ;
les tangentes personnalisées du fichier ne sont pas conservées. UV0 et UV1 sont disponibles ; un matériau
qui demande UV2 ou plus est rejeté avec un diagnostic. Les filtres d'échantillonnage sont linéaires avec mipmaps.
Pour OBJ/FBX et les autres formats, les propriétés reprises dépendent de ce qu'Assimp expose ;
les anciens matériaux diffus reçoivent un métal nul et une rugosité dérivée de leur brillance, si disponible.
Les unités affichées sont celles du fichier ; la passerelle Unreal convertit les centimètres en mètres.

## Compiler

Windows : Visual Studio 2022, charge de travail C++, CMake et Ninja fournis par Visual Studio.

```powershell
.\build.ps1
.\build.ps1 -Config Debug
```

Linux : compilateur C++20, CMake 3.24+, Ninja et bibliothèques de développement OpenGL/X11 et libcurl.

```sh
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF
cmake --build build/Release --parallel
```

macOS : outils Xcode, CMake et Ninja ; mêmes commandes, sans l'option X11/Wayland.
Apple fournit encore OpenGL 3.3 via son profil core, mais l'API y est dépréciée.

Les dépendances sont téléchargées automatiquement au premier CMake, avec versions et SHA-256 épinglés.
Si le téléchargement CMake pose problème, `python tools/fetch_deps.py` prépare `third_party/`.
Les dépendances locales sont réutilisées ; aucun runtime C++ redistribuable n'est nécessaire pour le build MSVC statique.

## Vérifier et mesurer

```sh
ctest --test-dir build/Release --output-on-failure
build/Release/bin/cy3d_probe --test tests/fixtures
build/Release/bin/cy3d_probe mon_modele.glb 5
```

Les tests vérifient OBJ texturé, STL ASCII/binaire, PLY, glTF avec instances transformées,
GLB à texture embarquée, chemins accentués, fichiers invalides/tronqués/absents, annulation,
cache et ordre des requêtes. Les tests `gpu` exigent une session graphique (Xvfb + Mesa sous Linux).
Le corpus Assimp vérifie aussi FBX, Collada et 3MF, dont un FBX avec squelette et un Collada animé.
Le test GPU vérifie la préparation progressive,
l'annulation, le maintien de l'ancien affichage en cas d'erreur, les textures et le cache GPU.
Le test PBR vérifie les facteurs, les canaux ORM, transformations UV, normal maps, occlusion, émission,
exposition, couleur sRGB, alpha et le rendu réel de la scène de studio. Il génère des captures et
`build/Release/pbr-metrics.json`, avec un chronométrage GPU distinct du temps CPU affiché dans l'interface.
Le test étendu vérifie les clips, STEP, hiérarchie, déformation GPU, squelette, lecture/boucle,
sept encodages de nuages/splats, quatre extensions USD et les chemins accentués.
Les fixtures USDC/.blend sont produites par `tools/make_bridge_fixtures.py` exécuté dans Blender ;
les autres par `tools/make_extended_fixtures.py`. Ces fichiers générés sont livrés avec les sources.
Pour activer les tests locaux des passerelles, configurer CMake avec `CY3D_TEST_BLENDER` (exécutable)
et/ou `CY3D_TEST_UNREAL_ASSET` (mesh non cuit dans Content). Ils portent le label `integration`.
Le test `blender_texture_regression` reproduit les indices UV négatifs et vérifie les valeurs de matériau,
les UV valides et la conservation des données binaires. `--loading-smoke modele capture.png` produit
deux captures du chargement et une capture finale pour contrôler visuellement l'indicateur animé.

```sh
python tools/generate_benchmark.py --triangles 200000
build/Release/bin/cy3d_probe --plugin build/Release/bin/plugins/cy3d_stl.dll build/benchmark.stl
build/Release/bin/cy3d_probe --plugin build/Release/bin/plugins/cy3d_assimp.dll build/benchmark.stl
```

Ces mesures portent sur l'import CPU, sans envoi GPU. Elles doivent être répétées sur des modèles représentatifs
avant toute promesse de vitesse générale. Les imports Assimp utilisent leur mémoire de travail en plus de la scène
finale ; la limite de 2 Gio porte sur la sortie, pas sur la consommation maximale du processus.
L'envoi GPU est découpé en blocs de 1 Mio sur le thread graphique, avec un budget de soumission par tour.
L'allocation des buffers et la génération des mipmaps peuvent néanmoins dépasser ce budget selon le pilote.
Le modèle actif et la scène en préparation s'ajoutent aux limites des caches lorsqu'ils n'y tiennent pas.
Voir les [mesures initiales et leurs limites](docs/PERFORMANCE.md).

## Distribuer

```sh
cmake --install build/Release --prefix dist/Cy3DView
```

Le résultat contient l'application, les plugins, l'exemple, la documentation et les licences tierces.
L'installeur Windows ajoute les raccourcis et propose l'application dans « Ouvrir avec », sans changer
le logiciel par défaut choisi par l'utilisateur. La recherche des mises à jour reste manuelle.

Sous Windows, `powershell -File tools/package.ps1` compile, teste, prépare le dossier portable,
vérifie son rendu et produit l'archive ZIP et l'installeur portant la version du projet.
Inno Setup 6.7 ou ultérieur est nécessaire à la création de l'installeur ;
`tools/build_installer.ps1 -Compiler <chemin-vers-ISCC.exe>` permet de préciser son emplacement.
`-SkipBuild` réutilise le build existant. `tools/installer_test.ps1` vérifie une installation isolée,
sa mise à jour et sa désinstallation avec une identité Windows dédiée au test.
Le test vérifie les fichiers installés et l'import de modèles depuis les plugins installés.
Le rendu exige OpenGL 3.3. Sur les machines Windows virtuelles de CI, `-AllowMissingOpenGL`
tolère uniquement les messages explicites de pilote incompatible ; les autres erreurs restent bloquantes.
Les journaux de l'import et du rendu sont conservés dans le dossier de test. Les tests graphiques
restent obligatoires localement par défaut et dans la CI Linux avec Mesa/Xvfb.

Voir [les évolutions](docs/CHANGELOG.md), [l'API des plugins](docs/PLUGIN_API.md) et [les dépendances](docs/THIRD_PARTY.md).
