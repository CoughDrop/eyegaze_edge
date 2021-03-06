(function () {
    var cp = require('child_process');
    var fs = require('fs');
    var tracker = null;
    var latest = null;
    var edge = {
        setup: function (callback) {
            var dir = process.cwd() + "\\edge";
            var manually_closing = false;
            var responded = false;
            try {
                var test = cp.spawn(dir + "\\EdgeTracker.exe", {
                    cwd: dir
                });
                var str = "";
                test.stdout.setEncoding('utf-8');
                test.stdin.setEncoding('utf-8');
                var on_data = function (data) {
                    str = str + data;
                    if (data && data.match(/initialized/)) {
                        console.log('Eyegaze Edge is Available');
                        responded = true;
                        callback({ ready: true });
                        test.stdout.removeListener('data', on_data);
                    } else if (data) {
                        console.log("Eyegaze Edge data: " + data);
                    }
                };
                test.stdout.on('data', on_data);
                test.on('exit', function (code) {
                    if (responded) { return; }
                    if (!manually_closing || !str.match(/initialized/)) {
                        callback({ ready: false });
                    } else {
                        callback({ ready: true });
                    }
                });
                // Reuse for listening for events in the case where speak mode 
                // initializes before the setup check is finished.
                tracker = {process: test};
            } catch (e) {
                if (!responded) {
                    callback({ ready: false });
                }
            }
            setTimeout(function () {
                manually_closing = true;
                if(!tracker || (!tracker.in_use && tracker.process == test)) {
                  if(!test.killed) {
                    console.log("Eyegaze Edge killing setup process");
                    test.stdin.write('q\n');
                    test.stdout.read();
                    test.kill();
                  }
                }
            }, 10000);
        },
        teardown: function () {
            edge.listening = false;
            if (tracker && tracker.process && !tracker.process.killed) {
                console.log("Eyegaze Edge tearing down");
                tracker.process.stdin.write('q\n');
                tracker.process.stdout.read();
                tracker.process.kill();
                tracker = null;
            }
        },
        handle_data: function(data) {
            console.log("query result", data);
            var lines = (data || "").split(/\n/);
            var dimensions = {};
            for (var idx = 0; idx < lines.length; idx++) {
                if (lines[idx]) {
                    parts = lines[idx].split(/,/);
                    if (parts.length == 3) {
                        var x = parseFloat(parts[0].replace(/\s+/g, ''));
                        var y = parseFloat(parts[1].replace(/\s+/g, ''));
                        var ts = parseFloat(parts[2].replace(/\s+/g, ''));
                        if (isFinite(x) && isFinite(y) && ts > 0) {
                            latest = {
                                gaze_x: x,
                                gaze_y: y,
                                gaze_ts: ts,
                                scaled: true
                            };
                        }
                    } else {
                        parts = lines[idx].split(/\s+/);
                        var width = parseFloat(parts[0] || 'none');
                        var height = parseFloat(parts[1] || 'none');
                        if (isFinite(width) && isFinite(height)) {
                            dimensions.width = width;
                            dimensions.height = height;
                        }
                    }
                }
            }
            setTimeout(edge.query, 45);
        },
        query: function() {
            if (tracker && tracker.process) {
                tracker.process.stdin.write('read\n');
            }
        },
        listen: function () {
            edge.listening = true;
            if (tracker && tracker.process && !tracker.process.killed) { 
              console.log("Eyegaze Edge using existing process");
              tracker.process.stdin.setEncoding('utf-8');
              tracker.process.stdout.setEncoding('utf-8');
              tracker.process.stdout.on('data', edge.handle_data);
              tracker.in_use = true;
              setTimeout(edge.query, 100);
              return;
            }
            var dir = process.cwd() + "\\edge"
            tracker = {
              in_use: true,
              process: cp.spawn(dir + "\\EdgeTracker.exe", {
                  cwd: dir
              })
            };
            tracker.process.stdin.setEncoding('utf-8');
            tracker.process.stdout.setEncoding('utf-8');
            tracker.process.stdout.on('data', edge.handle_data);
            setTimeout(edge.query, 100);
        },
        calibrate: function (callback) {
            callback = callback || (function (res) { console.log(res); });
            var dir = process.cwd() + "\\edge"
            try {
                fs.mkdir('C:\\Eyegaze', function (err) {
                    if (err && err.code && err.code != 'EEXIST') {
                        callback({ calibrated: false });
                        return;
                    }
                    var calibrate = cp.spawn(dir + "\\Calibrate.exe", {
                        cwd: dir
                    });
                    var str = "";
                    calibrate.stdout.setEncoding('utf-8');
                    calibrate.stdout.on('data', function (data) {
                        str = str + data;
                    });
                    calibrate.on('exit', function (code) {
                        console.log("output:", str);
                        if (fs.existsSync("C:\\Eyegaze\\calibration.dat")) {
                            cp.exec("copy /y C:\\Eyegaze\\calibration.dat " + dir + "\\calibration.dat");
                            callback({ calibrated: true });
                        } else {
                            callback({ calibrated: false });
                        }
                    });
                });
            } catch (e) {
                callback({ calibrated: false });
            }
        },
        stop_listening: function () {
            edge.listening = false;
            if(tracker) {
              tracker.in_use = false;
            }
            setTimeout(function() {
              if (tracker && !tracker.in_use && tracker.process && !tracker.process.killed) {
                  console.log("Eyegaze Edge stopping listening");
                  tracker.process.stdin.write('q\n');
                  tracker.process.stdout.read();
                  tracker.process.kill();
                  tracker = null;
              }
            }, 10000);
        },
        ping: function () {
            return latest;
        }
    };
    module.exports = edge;
})();