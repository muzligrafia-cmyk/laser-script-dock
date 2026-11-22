// Dompi Points – random circle placer for Adobe Illustrator (v6)
// Places up to six 5 mm circles on the active artboard.
// Rules:
//   - at least 15 mm from the artboard edge
//   - fill: #000000 (RGB docs) or C60 M40 Y40 K100 (CMYK docs)
//   - circles never overlap each other
//   - at least 10 mm clearance from every existing object / clipping mask
//     (true Bezier contours for paths/masks, bounding boxes for text etc.;
//     raster images are excluded)
//   - falls back 6 -> 5 -> 4 ... and alerts the user about the final count
//   - circles are created on a layer named "Dompi Points"
// v5: even distribution via best-candidate sampling.
// v6: understands the "Illesztőkeret" layer made by Illesztokeret.jsx:
//   - stroke-only, unfilled paths on that layer (the frame) are treated as
//     lines, not areas: circles MAY sit inside the frame, but each circle's
//     edge stays at least 5 mm from the outer edge of the stroke
//   - everything else on that layer (corner squares, label text) is a normal
//     obstacle with the usual 10 mm clearance
//   - the Illesztőkeret layer is respected even when it is hidden

#target illustrator

(function () {

    if (app.documents.length === 0) {
        alert("Open a document first!");
        return;
    }

    var doc = app.activeDocument;

    // ---------- constants ----------
    var MM             = 2.834645669;       // points per millimeter
    var RADIUS         = 2.5 * MM;          // 5 mm diameter
    var EDGE_MARGIN    = 15  * MM;          // min distance circle -> artboard edge
    var CLEARANCE      = 10  * MM;          // min distance circle -> existing artwork
    var NEED           = RADIUS + CLEARANCE;// required distance: center -> contour
    var FRAME_CLEARANCE= 5   * MM;          // min distance circle edge -> frame stroke edge
    var MAX_CIRCLES    = 6;
    var ATTEMPTS       = 4000;              // random tries per circle
    var CANDIDATES     = 40;                // valid candidates to gather per circle
    var SAMPLES_PER_SEG= 16;                // Bezier sampling resolution
    var LAYER_NAME     = "Dompi Points";
    var FRAME_LAYER    = "Illeszt\u0151keret"; // "Illesztőkeret"

    function mm(points) { return Math.round(points / MM * 100) / 100; }

    // ---------- active artboard ----------
    var abIndex = doc.artboards.getActiveArtboardIndex();
    var ab      = doc.artboards[abIndex];
    var rect    = ab.artboardRect;          // [left, top, right, bottom]
    var abW     = rect[2] - rect[0];
    var abH     = rect[1] - rect[3];

    // allowed region for circle CENTERS
    var minX = rect[0] + EDGE_MARGIN + RADIUS;
    var maxX = rect[2] - EDGE_MARGIN - RADIUS;
    var maxY = rect[1] - EDGE_MARGIN - RADIUS;
    var minY = rect[3] + EDGE_MARGIN + RADIUS;

    if (minX > maxX || minY > maxY) {
        alert("No circles can be added!\n\n" +
              "Reason: the active artboard ('" + ab.name + "', #" + (abIndex + 1) + ") is too small.\n" +
              "Artboard size: " + mm(abW) + " x " + mm(abH) + " mm\n" +
              "Minimum required: 35 x 35 mm\n" +
              "(15 mm margin + 2.5 mm radius on every side)");
        return;
    }

    // ---------- raster helpers ----------
    var RASTER_EXT = /\.(jpe?g|png|tiff?|psd|gif|bmp|webp|heic|heif)$/i;

    function isRaster(item) {
        if (item.typename === "RasterItem") return true;
        if (item.typename === "PlacedItem") {
            try {
                if (item.file && RASTER_EXT.test(item.file.name)) return true;
            } catch (e) {} // broken link: treat as obstacle (safe side)
        }
        return false;
    }

    // ---------- Bezier sampling ----------
    function samplePath(p) {
        var pts = p.pathPoints;
        var n = pts.length;
        if (n === 0) return null;
        if (n === 1) return { pts: [[pts[0].anchor[0], pts[0].anchor[1]]], closed: false };

        var poly = [];
        var segCount = p.closed ? n : n - 1;
        for (var i = 0; i < segCount; i++) {
            var a = pts[i];
            var b = pts[(i + 1) % n];
            var p0x = a.anchor[0],         p0y = a.anchor[1];
            var c1x = a.rightDirection[0], c1y = a.rightDirection[1];
            var c2x = b.leftDirection[0],  c2y = b.leftDirection[1];
            var p3x = b.anchor[0],         p3y = b.anchor[1];

            for (var s = 0; s < SAMPLES_PER_SEG; s++) {
                var t = s / SAMPLES_PER_SEG;
                var u = 1 - t;
                var x = u*u*u*p0x + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*p3x;
                var y = u*u*u*p0y + 3*u*u*t*c1y + 3*u*t*t*c2y + t*t*t*p3y;
                poly.push([x, y]);
            }
        }
        if (!p.closed) poly.push([pts[n - 1].anchor[0], pts[n - 1].anchor[1]]);
        return { pts: poly, closed: p.closed };
    }

    function itemToPolys(item) {
        var polys = [];
        if (item.typename === "PathItem") {
            var pl = samplePath(item);
            if (pl !== null) polys.push(pl);
        } else if (item.typename === "CompoundPathItem") {
            for (var i = 0; i < item.pathItems.length; i++) {
                var pl2 = samplePath(item.pathItems[i]);
                if (pl2 !== null) polys.push(pl2);
            }
        }
        return polys.length > 0 ? polys : null;
    }

    // ---------- geometry ----------
    function distToRect(px, py, gb) {
        var L = gb[0], T = gb[1], R = gb[2], B = gb[3];
        var dx = 0, dy = 0;
        if (px < L)      dx = L - px;
        else if (px > R) dx = px - R;
        if (py > T)      dy = py - T;
        else if (py < B) dy = B - py;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function distToSegment(px, py, ax, ay, bx, by) {
        var dx = bx - ax, dy = by - ay;
        var len2 = dx * dx + dy * dy;
        var t = 0;
        if (len2 > 0) {
            t = ((px - ax) * dx + (py - ay) * dy) / len2;
            if (t < 0) t = 0; else if (t > 1) t = 1;
        }
        var qx = ax + t * dx - px;
        var qy = ay + t * dy - py;
        return Math.sqrt(qx * qx + qy * qy);
    }

    function pointInPoly(px, py, pts) {
        var inside = false;
        for (var i = 0, j = pts.length - 1; i < pts.length; j = i++) {
            var xi = pts[i][0], yi = pts[i][1];
            var xj = pts[j][0], yj = pts[j][1];
            if (((yi > py) !== (yj > py)) &&
                (px < (xj - xi) * (py - yi) / (yi === yj ? 1e-12 : (yj - yi)) + xi)) {
                inside = !inside;
            }
        }
        return inside;
    }

    // need        = required center -> contour distance
    // allowInside = true for the frame (circles may sit inside it)
    function polyIsClear(px, py, poly, need, allowInside) {
        var pts = poly.pts;
        if (!allowInside && poly.closed && pts.length > 2 && pointInPoly(px, py, pts)) return false;
        var n = pts.length;
        var last = poly.closed ? n : n - 1;
        for (var i = 0; i < last; i++) {
            var a = pts[i], b = pts[(i + 1) % n];
            if (distToSegment(px, py, a[0], a[1], b[0], b[1]) < need) return false;
        }
        return true;
    }

    // ---------- collect obstacles (recursive walk) ----------
    var obstacles = [];
    var frames    = [];   // Illesztőkeret frame lines
    var skippedRasters = 0;

    var regionGB = [minX - NEED, maxY + NEED, maxX + NEED, minY - NEED]; // [L,T,R,B]
    function nearRegion(gb) {
        return !(gb[2] < regionGB[0] || gb[0] > regionGB[2] ||
                 gb[1] < regionGB[3] || gb[3] > regionGB[1]);
    }

    function addObstacle(item, label, forcePolys) {
        try {
            var gb = item.geometricBounds;
            if (!nearRegion(gb)) return;
            var polys = forcePolys !== undefined ? forcePolys : itemToPolys(item);
            obstacles.push({ gb: gb, polys: polys, label: label });
        } catch (e) {}
    }

    // frame: stroke-only, unfilled path on the Illesztőkeret layer
    function isFrameShape(p) {
        try { return p.stroked && !p.filled; } catch (e) { return false; }
    }

    function addFrame(item) {
        try {
            if (!nearRegion(item.geometricBounds)) return;
            var polys = itemToPolys(item);
            if (polys === null) return;
            // distance is measured to the stroke's outer edge, so add half the stroke width
            var half = item.strokeWidth / 2;
            frames.push({ polys: polys, need: RADIUS + FRAME_CLEARANCE + half });
        } catch (e) {}
    }

    function findClipPath(group) {
        for (var i = 0; i < group.pageItems.length; i++) {
            var it = group.pageItems[i];
            try {
                if (it.typename === "PathItem" && it.clipping) return it;
                if (it.typename === "CompoundPathItem" &&
                    it.pathItems.length > 0 && it.pathItems[0].clipping) return it;
            } catch (e) {}
        }
        return group.pageItems.length > 0 ? group.pageItems[0] : null;
    }

    function walkItems(container, layerName, isFrameLayer) {
        for (var i = 0; i < container.pageItems.length; i++) {
            var it = container.pageItems[i];
            try {
                if (it.hidden && !isFrameLayer) continue;

                if (isRaster(it)) { skippedRasters++; continue; }

                if (it.typename === "GroupItem") {
                    if (it.clipped) {
                        var clip = findClipPath(it);
                        if (clip !== null) {
                            addObstacle(clip, "Clipping mask on layer '" + layerName + "'");
                        } else {
                            addObstacle(it, "Clipped group on layer '" + layerName + "'", null);
                        }
                    } else {
                        walkItems(it, layerName, isFrameLayer);
                    }
                    continue;
                }

                if (it.typename === "PathItem") {
                    if (it.guides) continue;
                    if (isFrameLayer && isFrameShape(it)) { addFrame(it); continue; }
                    addObstacle(it, "PathItem on layer '" + layerName + "'");
                    continue;
                }

                if (it.typename === "CompoundPathItem") {
                    addObstacle(it, "CompoundPathItem on layer '" + layerName + "'");
                    continue;
                }

                addObstacle(it, it.typename + " on layer '" + layerName + "'", null);
            } catch (e) {}
        }
    }

    function walkLayer(layer, inFrameLayer) {
        var isFrameLayer = inFrameLayer || layer.name === FRAME_LAYER;
        if (!layer.visible && !isFrameLayer) return;   // Illesztőkeret counts even if hidden
        if (layer.name === LAYER_NAME) return;
        walkItems(layer, layer.name, isFrameLayer);
        for (var i = 0; i < layer.layers.length; i++) walkLayer(layer.layers[i], isFrameLayer);
    }

    var i;
    for (i = 0; i < doc.layers.length; i++) walkLayer(doc.layers[i], false);

    // ---------- placement test ----------
    function spotIsFree(x, y, placed) {
        var j, k;
        for (j = 0; j < obstacles.length; j++) {
            var ob = obstacles[j];
            if (distToRect(x, y, ob.gb) >= NEED) continue; // fast accept
            if (ob.polys === null) return false;
            for (k = 0; k < ob.polys.length; k++) {
                if (!polyIsClear(x, y, ob.polys[k], NEED, false)) return false;
            }
        }
        for (j = 0; j < frames.length; j++) {
            var fr = frames[j];
            for (k = 0; k < fr.polys.length; k++) {
                if (!polyIsClear(x, y, fr.polys[k], fr.need, true)) return false;
            }
        }
        for (j = 0; j < placed.length; j++) {
            var dx = x - placed[j][0];
            var dy = y - placed[j][1];
            if (Math.sqrt(dx * dx + dy * dy) < 2 * RADIUS + 0.01) return false;
        }
        return true;
    }

    // ---------- try to place circles (best-candidate sampling) ----------
    var placed = [];
    var c, a, x, y;

    function minDistToPlaced(x, y) {
        var best = Infinity;
        for (var j = 0; j < placed.length; j++) {
            var dx = x - placed[j][0];
            var dy = y - placed[j][1];
            var d = Math.sqrt(dx * dx + dy * dy);
            if (d < best) best = d;
        }
        return best;
    }

    for (c = 0; c < MAX_CIRCLES; c++) {
        var bestX = null, bestY = null;
        var bestScore = -1;
        var foundCount = 0;

        for (a = 0; a < ATTEMPTS && foundCount < CANDIDATES; a++) {
            x = (minX === maxX) ? minX : minX + Math.random() * (maxX - minX);
            y = (minY === maxY) ? minY : minY + Math.random() * (maxY - minY);
            if (!spotIsFree(x, y, placed)) continue;

            foundCount++;
            if (placed.length === 0) { bestX = x; bestY = y; break; }

            var score = minDistToPlaced(x, y);
            if (score > bestScore) { bestScore = score; bestX = x; bestY = y; }
        }

        if (bestX === null) break;
        placed.push([bestX, bestY]);
    }

    if (placed.length === 0) {
        var msg = "No circles can be added!\n\n" +
                  "The artboard is big enough, but every random position was blocked.\n" +
                  "Obstacles considered: " + obstacles.length +
                  "  (raster images ignored: " + skippedRasters + ")\n" +
                  "Illesztokeret frames found: " + frames.length;
        if (obstacles.length > 0) {
            msg += "\n\nFirst few obstacles:";
            for (i = 0; i < Math.min(obstacles.length, 8); i++) {
                var gb = obstacles[i].gb;
                msg += "\n- " + obstacles[i].label +
                       " at [" + mm(gb[0]) + ", " + mm(gb[1]) + "] - [" + mm(gb[2]) + ", " + mm(gb[3]) + "] mm";
            }
            msg += "\n\nTip: unfilled paths, empty text frames and template leftovers " +
                   "all count. Check View > Outline and the Layers panel.";
        }
        alert(msg);
        return;
    }

    // ---------- fill colour depending on document colour mode ----------
    var fill;
    if (doc.documentColorSpace === DocumentColorSpace.RGB) {
        fill = new RGBColor();
        fill.red = 0; fill.green = 0; fill.blue = 0;          // #000000
    } else {
        fill = new CMYKColor();
        fill.cyan = 60; fill.magenta = 40; fill.yellow = 40; fill.black = 100;
    }

    // ---------- get or create the "Dompi Points" layer ----------
    var layer = null;
    try { layer = doc.layers.getByName(LAYER_NAME); } catch (e) { layer = null; }
    if (layer === null) {
        layer = doc.layers.add();
        layer.name = LAYER_NAME;
    }
    layer.visible = true;
    layer.locked = false;

    // ---------- draw the circles ----------
    for (i = 0; i < placed.length; i++) {
        x = placed[i][0];
        y = placed[i][1];
        var circle = layer.pathItems.ellipse(y + RADIUS, x - RADIUS, RADIUS * 2, RADIUS * 2);
        circle.filled    = true;
        circle.fillColor = fill;
        circle.stroked   = false;
    }

    // ---------- report ----------
    if (placed.length < MAX_CIRCLES) {
        alert("Only " + placed.length + (placed.length === 1 ? " circle was" : " circles were") + " added!");
    }

})();
