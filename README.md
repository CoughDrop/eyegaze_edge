## EyeGaze Edge

LC Technologies' EyeGaze Edge is a hardware-based eye-tracking solution. This is
the source for an executable that can be compiled to communicate with the
hardware and pass the data along to a listener.

### Installation and Usage

The vendor can provided needed libraries and dlls that will need to be included.
After that open `build/EdgeTracker.sln` and build as a Release target for a 32-bit 
architecture.

```bash
# install eyegaze_edge
npm install https://github.com/coughdrop/eyegaze_edge.git
md edge
cp node_modules/eyegaze_edge/build/Release/EdgeTracker.exe edge/EdgeTracker.exe
cp lc_libraries/*.dll edge/*.dll
```

```
var edge = require('eyegaze_edge');

edge.setup(function(state) {
  if(state.ready) {
    edge.listen();
    setInterval(function() {
      console.log(edge.ping());
    }, 50);
  }
});

setTimeout(function() {
  edge.stop_listening();
}, 10000);
```


### Technical Notes
We use this library as part of our Electron app for CoughDrop. In order for it to work,
it needs to be in the `edge` folder within the application, along with the required 
dlls provided by the vendor ([http://www.eyegaze.com]). As of publishing, the needed SDKs 
and documentation are available here: http://www2.eyegaze.com/updates/

When this library is installed, it should automatically be used by 
[gazelinger.js](https://github.com/CoughDrop/gazelinger) if also installed.

Also note, the dlls will only compile in x86, not x64.

### License

MIT
