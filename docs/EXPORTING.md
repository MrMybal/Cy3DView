# Conversion et outils

La barre **Export / Exporter** convertit la scène chargée en **GLB**, **glTF + BIN**,
**FBX binaire ou ASCII**, **OBJ + MTL**, **STL binaire** et **PLY binaire**.
Les textures nécessaires sont embarquées ou copiées dans un sous-dossier relatif.
Le calcul se déroule en arrière-plan avec progression animée et annulation.
Le fichier source et ses dépendances connues sont protégés ; les fichiers déjà présents
à destination sont affichés avant leur remplacement.

| Format | Contenu conservé |
|---|---|
| GLB / glTF 2 | Géométrie, instances, PBR, textures, UV0/UV1, transformations UV, couleurs, clips et skin ; STEP uniforme par canal |
| FBX binaire / ASCII | Géométrie, instances, UV0/UV1, couleurs, clips et skin ; matériaux Phong simplifiés ; STEP devient linéaire |
| OBJ + MTL | Géométrie et transformations appliquées, UV0, matériaux simples et textures ; sans animation ni skin |
| STL | Triangles en pose de repos, transformations appliquées ; sans matériaux, UV ni animation |
| PLY | Géométrie ou points, transformations appliquées, normales, UV0 et couleurs ; sans matériaux ni animation |

**FBX est expérimental** : vérifier les matériaux et les animations dans le logiciel de destination.
La conversion porte sur les données réellement importées : les données écartées par un lecteur
(morphs, matériaux procéduraux, etc.) ne peuvent pas être reconstituées. Les avertissements d'import
sont rappelés avant l'export. Les splats gaussiens demandent un exporteur spécialisé.
Les exports utilisent la pose de repos, pas la position courante de la timeline.
Le glTF conserve les transformations UV via `KHR_texture_transform` ; métal et rugosité doivent
partager leurs réglages UV pour être regroupés dans une texture ORM. Les transformations avec
cisaillement ou projection ne sont pas prises en charge. OBJ/FBX exigent la même transformation
pour les textures partageant un canal UV, qui est alors appliquée aux coordonnées exportées.
Le PLY conserve les couleurs par sommet, sans appliquer les couleurs des matériaux.

**Tools / Outils → UV atlas (xatlas)** ouvre un premier petit éditeur : résolution,
marge, qualité, choix UV0/UV1 et aperçu de l'atlas. **Apply to copy / Appliquer à la copie**
génère les UV en conservant la géométrie, les couleurs et les poids du squelette ; les coutures
peuvent dupliquer des sommets. **UV1** est choisi par défaut pour conserver UV0 ; si des textures
utilisent déjà le canal choisi, le panneau le signale. **Undo / Annuler** restaure l'état précédent
(une étape). Les changements restent en mémoire et peuvent être exportés ; ouvrir un autre
modèle les abandonne. L'aperçu affiche au plus 20 000 triangles pour rester réactif.

Les formats d'export et les outils sont des plugins indépendants, chargés à la demande.
Les lecteurs existants restent compatibles. Voir [l'API des extensions](PLUGIN_EXTENSIONS.md)
et les exemples `plugins/export` et `plugins/xatlas` pour ajouter un format ou un panneau d'éditeur.
Les en-têtes du SDK sont inclus dans la distribution.
