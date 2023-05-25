# RDF

## Support VectorAudio/0.5.0+

[VectorAudio](https://github.com/pierr3/VectorAudio) is an Audio-For-VATSIM ATC client for macOS and Linux. It provides better audio quality when EuroScope is running in a Windows virtual machine. This improved RDF plugin utilizes VectorAudio's SDK and sends HTTP GET request every second to get transmitting pilots *and controllers*.

## More Customizations

+ Random offset of circle to simulate measuring errors in real life.
+ Customizable circle radius in nautical miles (instead of pixels).
+ (Old feature) RGB settings for circle or line, and different color for concurrent transmission.

## Configurations

There are two ways to modify the plugin settings - by settings file and by commandline functions.

This table shows all configurable items.

|Entry Name|Command Line Keyword|Default Value|
|-|-|-|
|VectorAudioAddress|ADDRESS|127.0.0.1:49080|
|VectorAudioTimeout|TIMEOUT|300|
|VectorAudioPollInterval|POLL|200|
|VectorAudioRetryInterval|RETRY|5|
|RGB|RGB|255:255:255|
|ConcurrentTransmissionRGB|CTRGB|255:0:0|
|Radius|RADIUS|20|
|Threshold|THRESHOLD|-1|
|Precision|PRECISION|0|
|DrawControllers|CONTROLLER|0|

E.g. In settings files the default is like the following:

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
RDF Plugin for Euroscope:DrawControllers:0
END
```

+ **VectorAudioAddress** should include address and port only. E.g. 127.0.0.1:49080 or localhost:49080, etc.
+ **VectorAudioTimeout** is in milliseconds. For VectorAudio HTTP requests.
+ **VectorAudioPollInterval** is in milliseconds. For VectorAudio normal refresh.
+ **VectorAudioRetryInterval** is in seconds. If the plugin disconnets from VectorAudio, it will attempt to re-establish connection every 5 seconds by default.
+ **RGB, ConcurrentTransmissionRGB**, see *Previous README* below.
+ **Radius** is in nautical miles (if **Threshold**>0). The circle will get bigger and smaller when zooming in and out.
+ **Threshold** determines the behaviour of drawing.
  + If radius (to draw in pixel) is smaller than **Threshold**, it won't be drawn into a circle but a direction line (same as the situation when the target is outside of displayed area). If **Threshold**<0, there won't be any zooming effect, but instead a fixed **Radius** in pixel (same as versions before v1.3.0).
+ **Precision** is in nautical miles. It is the double of standard deviation in a normal distrubution, which means 97.72% of offset won't be farther than **Precision**. Using 0 means no random offset at all (same as versions before v1.3.0).
+ **DrawControllers** is compatible with both VectorAudio and AFV. It is used to cover OBS pilots especially in shared cockpit. Other transimitting controllers will be circled as well but without offset. 0 means OFF and other numeric value means ON. *(Sometimes the position of OBS pilots are incorrect due to Euroscope limitation.)*

When EuroScope is running, you can reload settings in *Settings File Setup* and then enter ***".RDF RELOAD"*** (case-insensitive) in command line.

For command line configurations, use ***".RDF KEYWORD VALUE"***, e.g. ***".RDF CTRGB 0:255:255"***. Also all command line functions are case-insensitive.

## Credits

+ [pierr3/VectorAudio](https://github.com/pierr3/VectorAudio): initiative.
+ [chembergj/RDF](https://github.com/chembergj/RDF): basic drawings and AFV message handling.
+ [LeoChen98](https://github.com/LeoChen98), [websterzh](https://github.com/websterzh): idea of using HTTP requests.
+ [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib): HTTP library.
+ [vaccfr/CoFrance](https://github.com/vaccfr/CoFrance): method to use async HTTP requests (deprecated since v1.3.2).
+ [Ericple/VATPRC-UniSequence](https://github.com/Ericple/VATPRC-UniSequence): method to use detached thread for HTTP requests.

## [Installation and Previous README](https://github.com/chembergj/RDF#rdf)
