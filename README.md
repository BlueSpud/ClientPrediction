# ClientPrediction

## What Is This Plugin?
This plugin enables client prediction / rollback with an authoritative server. 
It acomplishes the same task as the Network Prediction plugin that is bundled with Unreal, however this plugin has recently disabled physics. 
ClientPrediction can be used for general purpose client prediction / rollback, but it is really intended for physics.
Networked physics in Network Prediction was disabled because it wasn't performant and added too much complexity. For more details, see [here](https://github.com/EpicGames/UnrealEngine/blob/137d565974b861bb0d0727813353fe740dad4bcf/Engine/Plugins/Runtime/NetworkPrediction/readme.txt).
ClientPrediction avoids the problems by using an immediate mode physics simulation. This allows physics to be run at a fixed timestep while the engine is still ticking at a variable rate.
Moreover, ClientPrediction performs rollback per-actor, so a rollback also won't affect the entire world.

## A Note About Immediate Mode
Immediate mode physics makes several trade offs, namely that the simulation is running in a copy of the main physics scene. 
ClientPrediction is ideal for a game where a single actor is controlled by the player and where collision between players is rare / not critical.

