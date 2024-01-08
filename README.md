# RDF

Radio Direction Finder plugin for [EuroScope](https://www.euroscope.hu). Still actively maintained.

## Support *VectorAudio*/0.5.0+

[*VectorAudio*](https://github.com/pierr3/VectorAudio) is an officially recognized multi-platform Audio-For-VATSIM ATC client for Windows, macOS and Linux. This improved RDF plugin utilizes *VectorAudio*'s SDK and sends HTTP GET requests to get transmitting stations and RX/TX status.

## More Customizations

+ Random radio direction offsets to simulate measuring errors in real life.
+ Simulate precision via variable circle radius in nautical miles.
+ Hide radio-direction-finders for low altitude aircrafts.
+ (Old feature) RGB settings for circle or line, and different color for concurrent transmission.

## Integrate afv-bridge

Do the same work as [*afv-euroscope-bridge*](https://github.com/AndyTWF/afv-euroscope-bridge). Supports both *VectorAudio* and *Audio for VATSIM standalone client*. When a new radio station is set up for RX/TX, RDF detects its status and toggles respective channels in EuroScope.

+ For *Audio for VATSIM standalone client*, toggles are made instantly.
+ For *VectorAudio*, the status are updated at an interval of **VectorAudioRetryInterval**, by default 5 seconds. See below.

## Configurations

There are two ways to modify the plugin settings - by settings file and by commandline functions.

This table shows all configurable items.

|Entry Name|Command Line Keyword|Range|Default Value|
|-|-|-|-|
|VectorAudioAddress|ADDRESS||127.0.0.1:49080|
|VectorAudioTimeout|TIMEOUT|[100, 1000]|300|
|VectorAudioPollInterval|POLL|[100, +inf)|200|
|VectorAudioRetryInterval|RETRY|[1, +inf)|5|
|RGB|RGB|RRR:GGG:BBB|255:255:255|
|ConcurrentTransmissionRGB|CTRGB|RRR:GGG:BBB|255:0:0|
|Radius|RADIUS|(0, +inf)|20|
|Threshold|THRESHOLD||-1|
|Precision|PRECISION|[0, +inf)|0|
|LowAltitude|ALTITUDE L_____||0|
|HighAltitude|ALTITUDE H_____|[0, +inf)|0|
|LowPrecision|PRECISION L_____||0|
|HighPrecision|PRECISION H_____|[0, +inf)|0|
|DrawControllers|CONTROLLER|0 or 1|0|

For command line configurations, use `.RDF KEYWORD VALUE`, e.g. `.RDF CTRGB 0:255:255`. Replace "_____" with value in low/high altitude/precision directly, e.g. `.RDF ALTITUDE L10000`. All command line functions are case-insensitive. Command line settings will be saved to plugin settings file (defined in .prf file).

In settings files the default is like the following:

```text
PLUGINS
<eventually existing configuration lines>
RDF Plugin for Euroscope:VectorAudioAddress:127.0.0.1:49080
RDF Plugin for Euroscope:VectorAudioTimeout:300
RDF Plugin for Euroscope:VectorAudioPollInterval:200
RDF Plugin for Euroscope:VectorAudioRetryInterval:5
RDF Plugin for Euroscope:RGB:255:255:255
RDF Plugin for Euroscope:ConcurrentTransmissionRGB:255:0:0
RDF Plugin for Euroscope:Radius:20
RDF Plugin for Euroscope:Threshold:-1
RDF Plugin for Euroscope:Precision:0
RDF Plugin for Euroscope:LowAltitude:0
RDF Plugin for Euroscope:HighAltitude:0
RDF Plugin for Euroscope:LowPrecision:0
RDF Plugin for Euroscope:HighPrecision:0
RDF Plugin for Euroscope:DrawControllers:0
END
```

+ **VectorAudioAddress** should include address and port only. E.g. 127.0.0.1:49080 or localhost:49080, etc.
+ **VectorAudioTimeout** is in milliseconds. For HTTP requests.
+ **VectorAudioPollInterval** is in milliseconds. For transmission inquiries.
+ **VectorAudioRetryInterval** is in seconds. For re-establishing connection and updating RX/TX channels.
+ **RGB, ConcurrentTransmissionRGB**, see [README](#readme-for-legacy-versions) below.
+ **Radius, Threshold, Precision, LowAltitude, HighAltitude, LowPrecision, HighPrecision** see [Random Offset Schematic](#random-offset-schematic) below.
+ **DrawControllers** is compatible with both *VectorAudio* and *Audio for VATSIM standalone client*. Other transimitting controllers will be circled as well but without offset. 0 means OFF and other numeric value means ON.

When EuroScope is running, you can reload settings in *Settings File Setup* and then enter `.RDF RELOAD` (case-insensitive) in command line.

## Random Offset Schematic

+ **LowAltitude** in feet, is used to filter aircrafts. Only aircrafts not lower than this altitude will be radio-direction-found.
+ A circle will only be drawn within radar display area. Otherwise a line leading to the target is drawn.
+ Random offsets (when enabled) follow a normal distribution. 99.74% (-3σ ~ 3σ) of offsets are within given precision.
+ **Threshold < 0**:
  + **Radius** is in pixel. Circles are always drawn in fixed pixel radius.
  + **Precision** is used for random offset in nautical miles.
  + Low/High settings are ignored.
+ **Threshold >= 0**:
  + **Threshold** is in pixel. **Radius** is in nautical miles. **Precision** is in nautical miles.
  + Circle size will change according to zoom level. Circles are drawn only when its pixel radius is not less than **Threshold**. Otherwise a line leading to the target is drawn.
  + When **LowPrecision > 0**:
    + Deprecates **Radius**. All circle radius is determined by precision.
    + If **HighPrecision > 0 and HighAltitude > LowAltitude**:
      + Overrides **Precision**. Dynamaic precision is implemented taking aircraft altitude into account.
      + Precision (= radius) is linearly interpolated or extrapolated by altitude and low/high settings. `Precision = LowPrecision + (Altitude - LowAltitude) / (HighAltitude - LowAltitude) * (HighPrecision - LowPrecision)`
    + Otherwise **LowPrecision** precedes **Precision** when determining random offset.

## Known Issues

+ EuroScope may crash when using TopSky at the same time with certain TopSky settings due to conflicting API method to communicate with *Audio for VATSIM standalone client*. Goto *TopSkySettings.txt* and add `RDF_Mode=-1` to prevent such cases.
+ Do not simultaneously load this plugin along with the older version of RDF, or with the original *afv-euroscope-bridge* plugin, which may cause unexpected behavior.
+ *Audio for VATSIM standalone client* doesn't provide callsign for RX/TX, so this plugin has to guess the corresponding callsign and it don't guarantee 100% correct toggles. But it shouldn't affect text receive and transmit function.
+ When using professional correlation mode (S or C) in EuroScope, it's possible some aircraft won't be radio-direction-found because the plugin doesn't know the callsign for an uncorrelated radar target.
+ For dual pilot situation where the transmitting pilot logs in as observer, this plugin will try to drop the last character of the observer callsign and find again if this dropped character is between A-Z. This feature may cause inaccurate radio-direction.

## Credits

+ [pierr3/VectorAudio](https://github.com/pierr3/VectorAudio): initiative.
+ [chembergj/RDF](https://github.com/chembergj/RDF): basic drawings.
+ [AndyTWF/afv-euroscope-bridge](https://github.com/AndyTWF/afv-euroscope-bridge): *Audio for VATSIM standalone client* message handling.
+ [LeoChen98](https://github.com/LeoChen98), [websterzh](https://github.com/websterzh): idea of using HTTP requests.
+ [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib): HTTP library.
+ [vaccfr/CoFrance](https://github.com/vaccfr/CoFrance): method to use async HTTP requests (deprecated since v1.3.2).
+ [Ericple/VATPRC-UniSequence](https://github.com/Ericple/VATPRC-UniSequence): method to use detached thread for HTTP requests.

## [README for Legacy Versions](https://github.com/chembergj/RDF#rdf)
