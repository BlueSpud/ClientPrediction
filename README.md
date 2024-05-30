# ClientPrediction

## What Is This Plugin?
This plugin enables client prediction / rollback with an authoritative server.
This primary purpose of this plugin is to enable replicated player controlled pawns driven by physics. The new physics replication in 5.4 can also do this, however the prediction / interpolation can behave strangely with simulated proxies. To overcome this, ClientPrediction does no forward prediction for simulated proxies and instead resorts to snapshot interpolation. ClientPrediction should be compatible with the built in physics replication, but since simulated proxies are not predicted, physics interractions with them might not work well. 

## Example
An example project can be found [here](https://github.com/BlueSpud/ClientPredictionExample)
Note: This is outdated as of 5.4 and is unlikely to be updated.
