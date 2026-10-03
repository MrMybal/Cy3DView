# Évolutions

## 0.5.0 — 3 octobre 2026

- Installeur Windows par utilisateur, sans droits administrateur, anglais/français, logo, licence GPL, raccourcis et désinstallation Windows.
- Mise à jour d'une installation existante sans suppression des préférences ni des plugins supplémentaires.
- Bouton Updates / Mises à jour : recherche asynchrone des versions stables GitHub, notes et progression de téléchargement.
- Vérification obligatoire de la taille et de l'empreinte SHA-256 de l'installeur ; ouverture après confirmation et fermeture de l'application.
- Accès facultatif au dépôt privé par jeton en mémoire uniquement ; aucune connexion nécessaire lorsque le dépôt est public.
- Sous Linux/macOS, recherche des versions et accès à la page de téléchargement ; installation manuelle du paquet approprié.
- Correction de la casse du dossier Collada dans les tests des modèles amont pour Linux.

## 0.4.4 — 3 octobre 2026

- Compilation macOS avec la zlib du SDK système, pour éviter l'incompatibilité de la copie Assimp avec les SDK Apple récents.
- Distribution sous GNU GPL v3.0, notices tierces et mentions de licence dans la fenêtre des plugins.
- Première distribution Windows portable avec logo, PBR, plugins, interface bilingue et déplacement clavier.

## 0.4.3 — 3 octobre 2026

- Interface en anglais par défaut ; sélecteur English / Français et choix mémorisé dans le dossier portable.
- Catalogue central pour commandes, matériaux PBR, animations, chargement, captures et diagnostics intégrés.
- Identifiants des contrôles et fenêtres conservés lorsque la langue change ; chemins et noms d'assets préservés.
- Déplacements continus WASD/ZQSD dans la direction de la caméra, Maj pour accélérer quatre fois.
- Vitesse indépendante du nombre d'images par seconde, diagonales normalisées et vitesse adaptée au modèle/zoom.
- Déplacements bloqués pendant la saisie, les dialogues, les contrôles, le chargement et la perte de focus.
- Plan de coupe rapproché pendant les déplacements pour permettre d'entrer dans la géométrie ; Home recadre la scène.
- Raccourci filaire déplacé de W vers F ; rendu continu seulement pendant les déplacements actifs.
- Sélecteur de fichiers Windows localisé et filtre construit depuis tous les formats des plugins installés.
- Tests de navigation, protection de la saisie, cohérence des traductions, identifiants et préférences ; captures UI dans les deux langues.

## 0.4.2 — 3 octobre 2026

- Chargement visible dans un bandeau de 94 pixels : indicateur tournant, barre animée,
  étape, temps écoulé et annulation. La conversion reste animée même sans pourcentage connu.
- Progression de l'envoi GPU avec pourcentage et bandes mobiles.
- Correction des exports Blender avec `texCoord=-1` : UV0 si disponible, sinon valeurs PBR
  du matériau source pour les textures sans coordonnées exportables. Le `.blend` reste inchangé.
- Avertissements de conversion conservés dans le cache et affichés après réouverture.
- Test de régression des références UV et de l'intégrité des données binaires GLB.
- Mode diagnostic `--loading-smoke modele capture.png` : deux captures pendant le chargement,
  puis capture du modèle prêt, pour vérifier les mouvements de la barre.

## 0.4.1 — 3 octobre 2026

- Logo violet fourni intégré à l'en-tête, à l'accueil et à la fenêtre des plugins.
- Icônes de fenêtre multirésolution et icône Windows de l'exécutable.
- Images embarquées dans le binaire, préparées une fois au démarrage.

## 0.4.0 — 3 octobre 2026

- Clips d'animation, hiérarchie, quatre influences d'os par sommet et déformation sur le GPU.
- Lecture/pause, boucle, timeline, choix de clip/vitesse et superposition du squelette.
- Préservation de STEP glTF, interpolation sphérique des rotations et messages pour les fonctions partielles.
- Couleurs de sommets et plugin natif pour PLY de points ASCII/little/big endian, XYZ et PTS.
- Rendu des Gaussian Splats anisotropes `.splat` et PLY, avec transparence et tri radix en profondeur.
- Lecteur expérimental USD/USDA/USDC/USDZ via tinyusdz, vérifié sur les quatre extensions.
- Passerelle Blender récent avec scripts du fichier désactivés, animations/skin et cache de conversion.
- Passerelle Unreal optionnelle pour meshes non cuits, conversion en GLB dans un projet temporaire.
  La racine Content et les dossiers créés par l'éditeur restent dans ce projet temporaire.
- Cache de conversion de 256 Mio, dépendances suivies, annulation et limite de durée des processus.
- ABI plugin v3 et repli vers un autre plugin pour un sous-ensemble explicitement décliné.
- Tests de rendu/animation/nuages/USD et tests d'intégration optionnels Blender/Unreal.

Restent partiels : compositions USD avancées, matériaux procéduraux, morph targets, tangentes CUBICSPLINE,
coefficients SH directionnels des splats et packages Unreal cuits/classes spécifiques de projet.
Les passerelles nécessitent un logiciel externe et leur première conversion prend plusieurs secondes.

## 0.3.0 — 3 octobre 2026

- Rendu PBR metallic/roughness avec BRDF GGX, Fresnel et éclairage de studio HDR généré sur le GPU.
- Cubemaps de reflets préfiltrés, convolution diffuse et table BRDF calculées une fois au démarrage.
- Import des textures métal, rugosité, normales, occlusion et émission ; facteurs et canaux ORM glTF.
- UV0/UV1, transformations UV glTF, répétition/clamp/miroir, unlit et intensité d'émission.
- Filtrage sRGB pour les couleurs/émission et linéaire pour les données, avec représentation distincte
  lorsqu'une image est utilisée dans les deux espaces.
- Tone mapping et contrôles d'exposition, intensité/orientation du studio, relief et occlusion.
- Modes alpha opaque/mask/blend ; passe transparente après les opaques et tri par centre d'instance.
- Démonstration glTF avec 18 sphères comparant métal et rugosité.
- ABI plugin **v2**, avec migration documentée et rejet explicite des modules v1.
- Test PBR sur le rendu GPU, conversions colorimétriques, matériaux, textures, UV et alpha.

Limites : workflow metallic/roughness, pose statique, studio intégré, sans ombres portées ni extensions
clearcoat/transmission/sheen/anisotropie. La transparence ne trie pas les triangles qui se croisent.

## 0.2.0 — 2 octobre 2026

- Transfert GPU par blocs de 1 Mio, avec budget de soumission de 4 Mio et environ 4 ms par tour.
  Le pilote peut dépasser le budget pendant une allocation ou la création des mipmaps.
- Le modèle courant reste affiché jusqu'à ce que la nouvelle scène soit entièrement prête.
- Annulation pendant l'import ou le transfert GPU via Échap et le bouton Annuler.
- Un échec d'envoi GPU conserve l'ancien affichage et libère les ressources partielles.
- Cache GPU LRU estimé de 256 Mio, en complément du cache CPU de 256 Mio.
  Il réutilise les buffers d'une scène inchangée et n'immobilise pas sa géométrie CPU.
- Calcul des matrices de normales une seule fois par instance ; orientations miroir corrigées.
- Localisations des uniformes et géométrie de la grille mises en cache.
- Explorateur limité aux lignes visibles ; noms et tri préparés pendant la lecture du dossier.
- Navigation par index et préchargement des voisins après l'ouverture initiale d'un fichier.
- Rotation automatique et transfert actif cadencés par la synchronisation verticale, sans attente supplémentaire.
- Tests FBX binaire, FBX avec texture embarquée, Collada instancié et 3MF issus du corpus Assimp.
- Tests GPU sur 200 000 triangles, texture 2048×2048, annulation, erreur, cache, éviction et indépendance vis-à-vis des buffers CPU.
- Script de packaging Windows avec vérification du rendu depuis le dossier portable.

## 0.1.0

Première application native portable, plugins Assimp et STL dédié, rendu de modèles texturés,
caméra orbitale, modes d'affichage, explorateur, chargement asynchrone et cache CPU.
