# Lux AI Season 2.NeurIPS - ry_andy_

This C++ agent placed 1st out of 64 teams in [Lux AI Season 2 - NeurIPS Edition](https://www.kaggle.com/competitions/lux-ai-season-2-neurips-stage-2/overview).

The Lux AI Challenge is a competition where competitors design agents to tackle a multi-variable optimization, resource gathering, and allocation problem in a 1v1 scenario against other competitors. In addition to optimization, successful agents must be capable of analyzing their opponents and developing appropriate policies to get the upper hand.

You can read my post-competition writeup from Season 2 [here](https://www.kaggle.com/competitions/lux-ai-season-2/discussion/407982).

## To setup and run

- Clone this repo and the [v3.0.1 runner](https://github.com/Lux-AI-Challenge/Lux-Design-S2/tree/v3.0.1)
- Follow setup instructions described [here](https://github.com/Lux-AI-Challenge/Lux-Design-S2/blob/v3.0.1/README.md#getting-started)
- From the Lux-S2-neurips-public directory, run `./compile -d`
- From the Lux-S2-neurips-public directory, run a test match `luxai-s2 ./build/agent.out ./build/agent.out -v 1 -o replay.html -s 0 -l 1000`
- To view a replay of the match, open replay.html e.g. `file:///path/to/Lux-S2-neurips-public/replay.html` in a browser (you will have to modify the path in the URL).
