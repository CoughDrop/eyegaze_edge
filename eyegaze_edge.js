(function () {
    var cp = require('child_process');
    var fs = require('fs');
    var tracker = null;
    var latest = null;
    var edge = {
        setup: function (callback) {
            var dir = process.cwd() + "\\edge"
            var manually_closing = false;
            try {
                var test = cp.spawn(dir + "\\EdgeTracker.exe", {
                    cwd: dir
                });
                var str = "";
                test.stdout.setEncoding('utf-8');
                test.stdout.on('data', function (data) {
                    str = str + data;
                });
                test.on('exit', function (code) {
                    console.log("output:", str);
                    if (!manually_closing || !str.match(/initialized/)) {
                        callback({ ready: false });
                    } else {
                        callback({ ready: true });
                    }
                });
            } catch (e) {
                callback({ ready: false });
            }
            setTimeout(function () {
                manually_closing = true;
                test.stdin.write('q\n');
                test.stdout.read();
                test.kill();
            }, 1000);
        },
        teardown: function () {
            if (tracker) {
                tracker.stdin.write('q\n');
                tracker.stdout.read();
                tracker.kill();
                tracker = null;
            }
        },
        query: function() {
            if (tracker) {
                tracker.stdin.write('read\n');
                var data = tracker.stdout.read();
                var lines = (data || "").split(/\n/);
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
                                    scaled: false
                                };
                            }
                        }
                    }
                }
                setTimeout(edge.query, 45);
            }
        },
        listen: function () {
            if (tracker) { return; }
            var dir = process.cwd() + "\\edge"
            tracker = cp.spawn(dir + "\\EdgeTracker.exe", {
                cwd: dir
            });
            tracker.stdin.setEncoding('utf-8');
            tracker.stdout.setEncoding('utf-8');
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
                    var test = cp.spawn(dir + "\\Calibrate.exe", {
                        cwd: dir
                    });
                    var str = "";
                    test.stdout.setEncoding('utf-8');
                    test.stdout.on('data', function (data) {
                        str = str + data;
                    });
                    test.on('exit', function (code) {
                        console.log("output:", str);
                        if (fs.existsSync("C:\\Eyegaze\\calibration.dat")) {
                            cp.spawn("copy /y C:\\Eyegaze\\calibration.dat " + dir + "\\calibration.dat");
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
            if (tracker) {
                tracker.stdin.write('q\n');
                tracker.stdout.read();
                tracker.kill();
                tracker = null;
            }
        },
        ping: function () {
            return latest;
        }
    };
    module.exports = edge;
})();