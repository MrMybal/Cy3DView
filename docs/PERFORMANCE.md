# Mesures initiales — 2 octobre 2026

Build Windows x64 Release, MSVC 19.40, AMD Ryzen 9 7900X, NVIDIA RTX 3090.
Mesures locales reproductibles, sans prétention de généraliser à tous les modèles.
Le tableau initial ci-dessous utilise les sommets ABI v1 de 32 octets. La version 0.3 passe à 40 octets
pour UV1 ; les scènes occupent donc davantage de mémoire. Les mesures 0.3 sont précisées plus bas.

## STL de 200 000 triangles

Le fichier de grille synthétique fait 10 000 084 octets. Il est produit par
`python tools/generate_benchmark.py --triangles 200000`.
Chaque lecteur est lancé dans son propre processus et importé cinq fois.
Le chronométrage couvre `load`, avant la libération ; il exclut le démarrage,
le chargement de la DLL, la validation par l'hôte, et le transfert GPU.
Les accès bénéficient du cache disque Windows ; ce n'est pas une mesure disque à froid.

| Lecteur | 5 imports, en ms | Médiane | Mémoire de scène déclarée |
|---|---|---|---|
| STL natif | 12,18 · 10,56 · 10,30 · 10,30 · 10,43 | 10,43 ms | 20,60 Mio |
| Assimp | 195,86 · 193,34 · 193,05 · 196,29 · 193,17 | 193,34 ms | 8,42 Mio |

Le lecteur dédié est environ **18,5 fois plus rapide sur ce test**.
Il conserve trois sommets par triangle et les normales de face, alors qu'Assimp
soude les sommets et génère des normales lissées. Le travail effectué et la mémoire
ne sont donc pas identiques. Cela illustre l'intérêt de plugins optimisés par format,
pas un gain universel du moteur.

## Comportement interactif

- L'interface reste active pendant l'import et le décodage des textures.
- Une nouvelle demande annule la précédente ; seul le résultat de la demande actuelle est affiché.
- Le cache de scènes est limité à 256 Mio ; les modèles plus gros restent ouvrables sans y entrer.
- Modèle actuel, mémoire temporaire d'import, textures et buffers GPU s'ajoutent à ce budget.
- Le rendu attend les événements lorsque la vue est immobile.
- Les touches WASD/ZQSD maintenues rendent en continu ; leur relâchement revient à l'attente des événements.
  Les déplacements utilisent le temps écoulé, avec diagonales normalisées, et sont bloqués pendant la saisie.
- La capture de démonstration affichait environ 0,18 ms de soumission CPU par rendu.
  Cette valeur n'est **pas** le temps de rendu GPU.

## Transfert progressif et cache GPU — version 0.2

Le test GPU utilise un STL de 200 000 triangles et des blocs limités à 1 Mio par appel de test.
La scène précédente est dessinée et ses pixels sont comparés entre les appels pour vérifier qu'elle reste intacte.
Sur la même machine, le fichier a nécessité **21 tours**, avec un appel maximal d'environ **7 à 8 ms**.
La sélection des buffers déjà présents dans le cache a pris moins de 0,01 ms côté CPU.
Cette dernière mesure exclut le chargement CPU, le traitement des événements et le rendu du modèle.
Le test de texture 2048×2048 a nécessité 18 tours.

Dans l'application, les paramètres par défaut sont des blocs de 1 Mio, jusqu'à 4 Mio soumis par tour,
et un objectif d'environ 4 ms de soumission. Les localisations d'uniformes, matrices de normales et buffers
de grille sont préparés une fois, puis réutilisés. Le cache GPU est estimé à 256 Mio et ne conserve
pas les sommets/indices CPU ; sa validité dépend de l'identité de la scène importée et du contrôle des dépendances.

## PBR — version 0.3, 3 octobre 2026

Même machine, build Release. Le test `pbr_render_contract` mesure le GPU avec une requête
OpenGL `GL_TIME_ELAPSED`, sur 20 rendus de la démonstration de 18 sphères (39 744 triangles),
après échauffement. Résultat local : environ **0,02 ms GPU par rendu à 1280×720**.
Cette scène utilise des facteurs de matériaux, le studio intégré et des sphères qui occupent une partie
du viewport ; ce chiffre ne prédit pas les performances d'une grande scène texturée ou la fréquence
réelle de l'interface. Il exclut la soumission CPU, les événements, ImGui et la présentation.
Le test génère `pbr-metrics.json` et une capture de la scène afin de contrôler ce qui a été dessiné.

La préparation de l'éclairage (cubemap 128² à huit niveaux, convolution diffuse 32² et table BRDF 128²)
est effectuée une fois par moteur. Environ **73 à 75 ms de soumission/initialisation CPU** observées ;
ce temps inclut compilation/link des shaders et initialisation du pilote, sans isoler le travail GPU.
Ces textures occupent environ 0,85 Mio de stockage logique, hors arrondi du pilote, et s'ajoutent au cache.
Changer de modèle n'effectue aucune nouvelle convolution. Les propriétés de matériau sont réutilisées
entre instances consécutives d'un même matériau. Le rendu continue à attendre les événements au repos.

Le test de transfert GPU de 200 000 triangles passe à **25 tours**, avec un maximum observé de **7,54 ms**
et un retour depuis le cache de **0,0004 ms côté CPU**, hors rendu. La texture 2048² nécessite encore 18 tours.
Un import STL natif de cinq passages a donné 34,42 · 10,49 · 10,58 · 11,20 · 10,32 ms,
soit une médiane de **10,58 ms**, pour **25,18 Mio** de sortie ABI v2. Le premier passage inclut
des coûts d'accès/allocation variables. Les modalités et limites du benchmark initial restent applicables.

Les images sont décodées une fois. Si une image sert à la fois de couleur sRGB et de données linéaires,
deux textures GPU sont nécessaires pour un filtrage correct ; leur coût est inclus dans l'estimation du cache.
Les mipmaps sont comptées approximativement à un tiers de la taille du niveau de base.

## Animations et nouveaux lecteurs — version 0.4

Sur la même machine, le test étendu importe le petit GLB animé en environ 2 à 3 ms et les nuages/splats
de 1 008 éléments en environ 0,4 à 1,4 ms. Ces fixtures servent à vérifier les formats et le rendu ;
leur petite taille ne permet pas de promettre une vitesse générale sur des scans lourds.
Les quatre variantes USD de test demandent généralement moins de 3 ms après chargement du plugin.

La géométrie statique garde ses sommets de 40 octets. Les tableaux facultatifs ajoutent 16 octets
de couleur, 32 octets de poids/indices de skin, ou 28 octets d'échelle/quaternion par sommet.
Les palettes de matrices d'os sont mises à jour lorsque la pose change ; les sommets restent sur le GPU.
Les clips/nœuds sont copiés indépendamment et leur mémoire est incluse dans l'estimation CPU.
La lecture animée rend en continu ; la pause permet à nouveau d'attendre les événements.

Le tri des splats est un radix de quatre passes sur une clé de profondeur 32 bits.
Il est refait lorsque l'œil, la cible ou la matrice d'instance change ; une vue immobile réutilise l'ordre.
Le cache de rendu compte aussi 20 octets de données CPU par splat (positions et deux tableaux d'indices) ;
un tableau temporaire de clés de 4 octets par splat s'ajoute pendant le tri.
Les positions sont préparées avec les transferts par blocs pour éviter une copie complète lors de l'allocation GPU.
Il n'y a pas encore de streaming, de LOD ou de tri sur le GPU.

Blender 5.2.1 : conversion du fichier animé de test en environ 2 à 4 secondes ; réimport depuis le GLB
converti en environ 3 à 5 ms. Unreal 5.6 : petit StaticMesh de projet en environ 10 à 32 secondes au premier
accès suivant l'état des caches du moteur ; réimport du GLB en environ 2 ms.
Ces mesures comprennent le lancement du logiciel externe, mais excluent l'envoi et le rendu GPU.
Ces passerelles favorisent la compatibilité des fichiers récents ; leurs conversions initiales sont beaucoup
plus longues que les lecteurs natifs. Un fichier converti supérieur à 256 Mio peut dépasser seul le budget
de conservation du cache disque ; les autres entrées sont alors évincées.

Les références USD externes ne sont pas toutes remontées par l'adaptateur amont : la réutilisation du cache
CPU est désactivée pour USD/USDZ afin d'éviter un affichage périmé après un changement de dépendance.

## Limites actuelles

Le premier transfert GPU peut payer des coûts d'initialisation du pilote. Les allocations de buffers
et la génération des mipmaps restent des appels individuels : le budget de 4 ms n'est pas une garantie
de durée maximale. Les importeurs Assimp peuvent utiliser
beaucoup de mémoire temporaire, et tous ne consultent pas l'annulation avec la même fréquence.
Le modèle actif et le transfert en cours peuvent dépasser les budgets de cache, notamment pour les scènes
plus grandes que 256 Mio. Les budgets GPU sont des estimations, pas une lecture de la mémoire réelle du pilote.

Les prochaines optimisations utiles sont des lecteurs directs glTF/OBJ, une gestion plus fine des textures
et des mesures sur un corpus représentatif de fichiers réels.
