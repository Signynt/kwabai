/* Original feasibility code. No automatic tiling or configuration execution. */
var kwabaiProbe = (function () {
    var selected = [], originals = [], changed = false;
    function uuid(w) { return String(w.internalId); }
    function geometry(w) {
        var r = w.frameGeometry;
        return {x:r.x, y:r.y, width:r.width, height:r.height};
    }
    function eligible(w) {
        return w && !w.deleted && !w.internal && !w.outline && !w.popupWindow &&
            w.normalWindow && !w.specialWindow && !w.minimized && !w.fullScreen &&
            !w.maximizeMode && w.moveable && w.resizeable;
    }
    function trace(event, w) {
        print('kwabai ' + JSON.stringify({event:event, uuid:uuid(w), frame:geometry(w)}));
    }
    function restore() {
        if (changed) selected.forEach(function (w,i) {
            if (eligible(w)) w.frameGeometry = originals[i];
        });
        changed = false;
    }
    function reset() { restore(); selected = []; originals = []; }
    function select() {
        var w = workspace.activeWindow;
        if (!eligible(w) || w.move || w.resize) return false;
        if (selected.length === 2) reset();
        if (selected.some(function (other) { return uuid(other) === uuid(w); })) return false;
        selected.push(w); originals.push(geometry(w)); trace('selected',w);
        return true;
    }
    function swap() {
        if (selected.length !== 2 || !selected.every(function(w) { return eligible(w) && !w.move && !w.resize; })) return false;
        var a=geometry(selected[0]), b=geometry(selected[1]);
        function fits(r,w) { return !w.minSize || (r.width >= w.minSize.width && r.height >= w.minSize.height); }
        if (!fits(a,selected[1]) || !fits(b,selected[0])) return false;
        changed = true;
        selected[0].frameGeometry=b; selected[1].frameGeometry=a;
        return true;
    }
    function observe(w) {
        if (!eligible(w)) return;
        w.interactiveMoveResizeStarted.connect(function () { trace('dragStart',w); });
        w.interactiveMoveResizeStepped.connect(function () { trace('dragStep',w); });
        w.interactiveMoveResizeFinished.connect(function () { trace('dragFinish',w); });
    }
    workspace.windowList().forEach(observe);
    workspace.windowAdded.connect(observe);
    workspace.windowRemoved.connect(function(w) {
        if (selected.some(function(other) { return uuid(other) === uuid(w); })) reset();
    });
    print('kwabai probe ready; no windows managed automatically');
    return {select:select, swap:swap, reset:reset};
}());
