# Bibliothèques

Les dépendances sont épinglées dans CMake et vérifiées par SHA-256 lors du téléchargement CMake.
`tools/fetch_deps.py` permet de préparer une copie locale ; aucune dépendance n'est téléchargée par l'application.

| Bibliothèque | Révision | Licence | Rôle |
|---|---|---|---|
| xatlas | f700c7790aaa030e794b52ba7791a05c085faf0c | MIT (Jonathan Young) | Génération et empaquetage des atlas UV |
| GLFW | 3.4 | zlib/libpng | Fenêtres et contexte OpenGL portable |
| Dear ImGui | 1.91.9b | MIT | Interface native |
| Assimp | 6.0.2 | BSD-3-Clause, voir les notices incluses | Import et export de formats 3D, uniquement dans les plugins |
| tinyusdz | 6050eef932f7d2788656d63297aa488fb0961ed1 | Apache-2.0 et composants inclus | Lecteur USD compatible avec l'adaptateur Assimp épinglé |
| stb | f0569113c93ad095470c54bf34a17b36646bbbb5 | MIT ou domaine public | Images de textures et captures PNG |
| GLAD | copie fournie par GLFW 3.4 | MIT/Apache-2.0, en-tête du fichier | Chargement des fonctions OpenGL |
| RapidJSON | copie fournie par Assimp 6.0.2 | MIT, notice incluse avec Assimp | Lecture des métadonnées GitHub et passerelles |
| libcurl | bibliothèque système Linux/macOS | curl, voir `licenses/curl.txt` | HTTPS pour la recherche de mises à jour |

L'installeur Windows est produit avec Inno Setup 6.7 ou ultérieur (Jordan Russell et Martijn Laan).
Ses composants intégrés conservent leur licence Inno Setup : `licenses/Inno-Setup.txt` dans le paquet.
Windows utilise WinHTTP et BCrypt du système pour le téléchargement et la vérification SHA-256.

Les licences des bibliothèques sont copiées dans `licenses/` par l'installation portable.
Sur macOS, Assimp utilise la zlib du SDK système ; Windows/Linux compilent la copie incluse avec Assimp.
Assimp contient d'autres composants et leurs notices : voir son fichier `LICENSE` et `contrib/`.
Les exemples et fichiers de test sont générés dans ce projet, sans asset tiers.
Le test local Unreal utilise éventuellement un asset du moteur installé, sans le distribuer dans les samples.
Les passerelles utilisent des installations externes Blender/Unreal ; aucun de ces logiciels n'est embarqué.
`tools/prepare_assimp.py` applique des ajustements reproductibles à Assimp : chemin de tinyusdz,
préservation et export STEP en glTF, export des transformations UV glTF et correction du
lookup d'indices FBX entre plusieurs canaux UV et initialisation des bornes d'accessors glTF et détection de l'extension volume. Les archives amont restent vérifiées par SHA-256.
Le PLY binaire est écrit directement par le plugin d'export pour garantir un en-tête cohérent.

Le validateur Khronos glTF (npm `gltf-validator` 2.0.0-dev.3.10, Apache-2.0) est utilisé seulement par la CI ; il ne fait pas partie de la distribution.
