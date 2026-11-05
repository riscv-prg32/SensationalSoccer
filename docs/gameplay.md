# Gameplay and presentation

## Screens

The front end has five screens:

1. **Title:** night sky, floodlights, animated crowd, ad boards, a running player and a waving gradient logo. The title music plays here. A, B or SELECT opens team selection; after ten idle seconds a 60-second AI demo match starts and then returns to the title. Any button ends the demo.
2. **Team selection:** LEFT/RIGHT changes the home team, UP/DOWN the away team. The two teams can never be the same. A confirms, B goes back.
3. **Mode selection:** a single 90-second fixture, or a three-round world tour where the away team rotates after every round.
4. **Match:** described below. SELECT returns to the title.
5. **Result:** CHAMPIONS!, TOUR OVER, YOU WIN!, DRAW or YOU LOSE, with the big score and the round. The result fanfare plays here. A continues.

Napoli plays in blue and white, London in red and white, Tokyo in yellow and navy, and Rio in green and yellow. Keepers wear magenta or grey. Players get one of four complexions, chosen deterministically.

## Match rules

- Six-a-side: one keeper and five outfield players per team, from a fixed formation.
- The clock shows 0'–90' and covers 90 real seconds (60 in the demo). It runs during restarts and stops during goal celebrations.
- **Kick-off:** the striker takes it and the whistle sounds after one second. The team that concedes kicks off.
- **Goal:** the ball must cross the line between the posts below the crossbar. Shots can hit the post or the crossbar.
- **Out of play:** a goal kick if the attacking team last touched it over the goal line, a corner if the defending team did, and a throw-in over a touchline.
- **Restarts:** the ball is dead and pinned in place. Only the restart taker can collect it, and opponents keep a 30-pixel distance. If the taker has not collected the ball after three seconds, it is handed to them, so a restart can never stall play.

## Control and AI

- **Possession:** the carrier dribbles the ball a few pixels ahead of their feet; you hear a soft touch on every few strides. Passes go to the best-placed team-mate. Shots aim for the corners, and stick aftertouch curls and dips them.
- **Defending:** an AI chaser closes the ball down and tries a standing tackle about every two thirds of a second; human carriers are harder to rob. A support player covers, and the others hold formation anchors that shift with the ball. Slide tackles knock the ball loose and stun the carrier briefly.
- **Keepers:** they position themselves on the line between ball and goal, predict incoming shots, dive, catch or parry, hold the ball for about 1.3 s, and then distribute it. A hard, well-placed shot can beat them. The chance grows with the distance between the ball and the keeper.
- **Blocks:** outfield players block only a quarter of the shots that reach them.
- **Tuning:** a deterministic host simulation of 94 AI-vs-AI demo minutes averages about one goal and three shots per demo minute. That is about 1.5 goals per 90-second match.

## Presentation

- **Stadium:** parallax grandstand, three-frame crowd animation (idle, arms up, celebration with scarves) in both teams' colours, and scrolling ad boards: RISC-V, GOAL TV, UNIPARTHENOPE, 8-BIT FC, PRG32, OPEN HW and others.
- **Pitch:** checkered grass stripes, full markings (halfway line, centre circle, boxes, arcs and spots), and goals with an oblique net that shakes after a goal.
- **Sprites:** depth sorting and ground shadows. The ball has four roll frames and rises above its shadow.
- **HUD:** team chips, score and minute on the top line; control hints and a pitch radar on the bottom line. Banners announce KICK OFF, GOAL!, FULL TIME, CORNER, GOAL KICK and THROW IN.

## Audio

Music plays only on the title (track 0, a 16-bar anthem) and the result screen (track 1, an 8-bar fanfare). Every other screen transition stops the tracker and all voices, and the match screen keeps the tracker stopped on every update. Match audio is event effects only, panned by world position:

- kick, whose pitch and loudness follow the shot power;
- dribble touch and tackle thud;
- ball bounce, scaled by impact speed;
- the referee's pea whistle: one blast for a kick-off and after a goal, three for full time;
- crowd roar on goals and crowd "ooh" on saves and near misses;
- post and crossbar clang, and net swish;
- menu blips.

The publication build sets master volume 160 in `sensational_soccer_init`. See `AGENTS.md` for the night-testing policy.
