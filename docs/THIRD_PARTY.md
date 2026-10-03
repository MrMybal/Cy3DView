# Bibliothèques

Les dépendances sont épinglées dans CMake et vérifiées par SHA-256 lors du téléchargement CMake.
`tools/fetch_deps.py` permet de préparer une copie locale ; aucune dépendance n'est téléchargée par l'application.

| Bibliothèque | Révision | Licence | Rôle |
|---|---|---|---|
| GLFW | 3.4 | zlib/libpng | Fenêtres et contexte OpenGL portable |
| Dear ImGui | 1.91.9b | MIT | Interface native |
| Assimp | 6.0.2 | BSD-3-Clause, voir les notices incluses | Import de formats 3D, uniquement dans le plugin |
| tinyusdz | 6050eef932f7d2788656d63297aa488fb0961ed1 | Apache-2.0 et composants inclus | Lecteur USD compatible avec l'adaptateur Assimp épinglé |
| stb | f0569113c93ad095470c54bf34a17b36646bbbb5 | MIT ou domaine public | Images de textures et captures PNG |
| GLAD | copie fournie par GLFW 3.4 | MIT/Apache-2.0, en-tête du fichier | Chargement des fonctions OpenGL |

Les licences des bibliothèques sont copiées dans `licenses/` par l'installation portable.
Assimp contient d'autres composants et leurs notices : voir son fichier `LICENSE` et `contrib/`.
Les exemples et fichiers de test sont générés dans ce projet, sans asset tiers.
Le test local Unreal utilise éventuellement un asset du moteur installé, sans le distribuer dans les samples.
Les passerelles utilisent des installations externes Blender/Unreal ; aucun de ces logiciels n'est embarqué.
`tools/prepare_assimp.py` applique deux ajustements reproductibles à Assimp : chemin de la dépendance tinyusdz,
et préservation des modes d'interpolation des animations glTF. Les archives amont restent vérifiées par SHA-256.
