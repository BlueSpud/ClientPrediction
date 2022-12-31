# ClientPrediction

## What Is This Plugin?
This plugin enables client prediction / rollback with an authoritative server. 
It accomplishes the same task as the Network Prediction plugin that is bundled with Unreal, however Network Prediction seems to be abandoned (see [here](https://forums.unrealengine.com/t/status-of-network-prediction-plugin-looking-for-epic-response/502509/7?u=bluespud)). 
Client Prediction currently assumes that there is only one actor that will be predicted and forces async physics. Due to an engine limitation, `Async Physics Tick Enabled` shouldn't be used for an actor. Instead, register a callback with `ClientPrediction::FWorldManager::AddTickCallback`.

The entire physics scene is not synced with the authority currently, so using dynamic objects is not supported right now. Additionally, sim proxies are not updated during the async physics steps which may cause strange interactions with auto proxies.

## Example
An example project can be found [here](https://github.com/BlueSpud/ClientPredictionExample)