// Adobe Illustrator Script: Refined Laser Janitor (V2.2)
//
// V2.2: Fixed duplicates being moved but not recoloured (happened with lines
//       pulled out of compound paths, and with zero/hairline strokes).
//
// V2.1: Partial overlaps.
//   - Lines that share a stretch of at least 1 mm (but where neither fully
//     covers the other) are marked on the "duplicates" layer with a thick
//     yellow copy of the shared stretch + a dashed yellow circle, grouped
//     and named "Overlap 1", "Overlap 2"... The original lines are untouched.
//   - Items already on the "duplicates" layer are ignored, so re-running the
//     script never treats its own markers as artwork.
// V2.0: Tolerant duplicate detection (0.05 mm), short-on-long lines,
//       direction/start point independent, compound path fixes.
// V1.4: Speck detection uses bounding box diagonal as well as area.

#target illustrator

// ===================== SETTINGS (safe to tweak) =====================
var DUP_TOLERANCE_MM   = 0.05;  // How close counts as "the same line"
var MIN_OVERLAP_MM     = 1.0;   // Shared stretch must be at least this long to be marked
var SAMPLE_STEP_MM     = 1.0;   // How often along a line we check for full duplicates
var FINE_STEP_MM       = 0.2;   // How finely partial overlaps are traced
var CURVE_STEP_MM      = 0.25;  // How finely curves are measured
var SPECK_AREA         = 8.035; // sq-pt: below this a shape is a candidate speck
var SPECK_DIAG_MM      = 2;     // mm: bounding box diagonal above this is never a speck
var POINT_MERGE_TOL    = 0.001; // pt: anchors closer than this get merged
var MARK_LINE_WIDTH    = 1.5;   // pt: thickness of the yellow overlap copy
var MARK_PADDING_MM    = 2;     // mm: space between overlap and circle
var MARK_MIN_RADIUS_MM = 3;     // mm: smallest circle drawn
var DUP_LAYER_NAME     = "duplicates";
// ====================================================================

var PT_PER_MM   = 2.834645669;
var TOL         = DUP_TOLERANCE_MM * PT_PER_MM;
var TOL_SQ      = TOL * TOL;
var MIN_OVERLAP = MIN_OVERLAP_MM * PT_PER_MM;
// Coarse checkpoints must be no further apart than the minimum overlap,
// otherwise a qualifying overlap could slip between two checkpoints.
var SAMPLE_STEP = Math.min(SAMPLE_STEP_MM, MIN_OVERLAP_MM) * PT_PER_MM;
var FINE_STEP   = FINE_STEP_MM * PT_PER_MM;
var CURVE_STEP  = CURVE_STEP_MM * PT_PER_MM;
var SPECK_DIAG  = SPECK_DIAG_MM * PT_PER_MM;

function refinedJanitor() {
    if (app.documents.length === 0) { alert("No document open."); return; }
    var doc = app.activeDocument;
    var sel = doc.selection;

    // --- STEP 1: Gather paths into a flat list ---
    var allPaths = [];
    if (sel && sel.length > 0) {
        findPaths(sel, allPaths);
    } else {
        var top = [];
        for (var t = 0; t < doc.pageItems.length; t++) {
            var it = doc.pageItems[t];
            if (it.parent.typename === "Layer") top.push(it);
        }
        findPaths(top, allPaths);
    }

    // --- STEP 2: Clean points & remove specks ---
    var specksRemoved = 0;
    var pointsCleaned = 0;
    for (var i = allPaths.length - 1; i >= 0; i--) {
        var item = allPaths[i];
        try {
            pointsCleaned += cleanPoints(item);
            if (item.pathPoints.length < 2 || isSpeck(item)) {
                item.remove();
                allPaths.splice(i, 1);
                specksRemoved++;
            }
        } catch (e) {
            allPaths.splice(i, 1);
        }
    }

    // --- STEP 3: Full duplicates (a line lying entirely on another) ---
    var infos = [];
    for (var p = 0; p < allPaths.length; p++) {
        var info = buildInfo(allPaths[p]);
        if (info) infos.push(info);
    }
    infos.sort(function (a, b) { return a.minX - b.minX; });

    var dupItems = [];
    for (var j = 0; j < infos.length; j++) {
        var A = infos[j];
        if (A.flagged) continue;
        for (var k = j + 1; k < infos.length; k++) {
            var B = infos[k];
            if (B.minX > A.maxX + TOL) break;
            if (B.flagged) continue;
            if (isCoveredBy(B, A)) {
                B.flagged = true;
                dupItems.push(B.item);
            } else if (isCoveredBy(A, B)) {
                A.flagged = true;
                dupItems.push(A.item);
                break;
            }
        }
    }

    // --- STEP 4: Partial overlaps (only between lines that stay) ---
    var overlaps = []; // each entry: array of [x, y] points along the shared stretch
    for (var j2 = 0; j2 < infos.length; j2++) {
        var A2 = infos[j2];
        if (A2.flagged) continue;
        for (var k2 = j2 + 1; k2 < infos.length; k2++) {
            var B2 = infos[k2];
            if (B2.minX > A2.maxX + TOL) break;
            if (B2.flagged) continue;
            // Trace along the shorter line of the pair
            if (A2.length <= B2.length) findPartialOverlaps(A2, B2, overlaps);
            else findPartialOverlaps(B2, A2, overlaps);
        }
    }

    // --- STEP 5: Move duplicates & draw overlap markers ---
    var moveFailed = 0;
    var markersDrawn = 0;
    if (dupItems.length > 0 || overlaps.length > 0) {
        var dupLayer = getDupLayer(doc);
        var yellow = new RGBColor();
        yellow.red = 255; yellow.green = 255; yellow.blue = 0;

        for (var m = 0; m < dupItems.length; m++) {
            if (!moveDuplicate(dupItems[m], dupLayer, yellow)) moveFailed++;
        }

        for (var o = 0; o < overlaps.length; o++) {
            try {
                drawOverlapMarker(dupLayer, overlaps[o], markersDrawn + 1, yellow);
                markersDrawn++;
            } catch (e) { }
        }
    }

    app.redraw();
    alert("Cleanup Complete!\n" +
          "- Specks Removed: " + specksRemoved + "\n" +
          "- Duplicate Points Removed: " + pointsCleaned + "\n" +
          "- Duplicates Moved to '" + DUP_LAYER_NAME + "' Layer: " + (dupItems.length - moveFailed) + "\n" +
          "- Partial Overlaps Marked: " + markersDrawn +
          (moveFailed > 0 ? "\n- Duplicates found but couldn't be moved: " + moveFailed : "") +
          "\n\n(Tolerance: " + DUP_TOLERANCE_MM + " mm, minimum overlap: " + MIN_OVERLAP_MM + " mm)");
}

// ===================== Layer & markers =====================

function getDupLayer(doc) {
    var layer;
    try {
        layer = doc.layers.getByName(DUP_LAYER_NAME);
    } catch (e) {
        layer = doc.layers.add();
        layer.name = DUP_LAYER_NAME;
    }
    layer.locked = false;
    layer.visible = true;
    return layer;
}

// Moves a duplicate to the duplicates layer and paints it yellow.
// Doesn't trust the reference returned by move(): when a path is pulled out
// of a compound path, that reference can go stale and the recolour silently
// fails. Instead we grab the item fresh from the top of the layer.
function moveDuplicate(item, layer, yellow) {
    var inCompound = false;
    try { inCompound = item.parent.typename === "CompoundPathItem"; } catch (e) { }

    // Standalone path: paint it before moving (the colour travels with it).
    // A compound sub-path can't be painted in place — that would recolour
    // the whole compound path — so it's painted after the move instead.
    if (!inCompound) {
        try { paintDuplicate(item, yellow); } catch (e) { }
    }

    try {
        item.move(layer, ElementPlacement.PLACEATBEGINNING);
    } catch (e) {
        return false;
    }

    // Fresh reference: the item we just placed at the top of the layer
    try { paintDuplicate(layer.pageItems[0], yellow); } catch (e) { }
    return true;
}

function paintDuplicate(p, yellow) {
    p.stroked = true;
    p.strokeColor = yellow;
    if (p.strokeWidth < 1) p.strokeWidth = 1; // Hairline/zero strokes would stay invisible
}

function drawOverlapMarker(layer, pts, number, yellow) {
    var g = layer.groupItems.add();
    g.name = "Overlap " + number;

    // Thick yellow copy of just the shared stretch
    var copy = g.pathItems.add();
    copy.setEntirePath(pts);
    copy.closed = false;
    copy.filled = false;
    copy.stroked = true;
    copy.strokeColor = yellow;
    copy.strokeWidth = MARK_LINE_WIDTH;
    copy.strokeCap = StrokeCap.ROUNDENDCAP;
    copy.name = "Overlapping stretch";

    // Dashed circle around it
    var minX = pts[0][0], maxX = pts[0][0], minY = pts[0][1], maxY = pts[0][1];
    for (var i = 1; i < pts.length; i++) {
        if (pts[i][0] < minX) minX = pts[i][0];
        if (pts[i][0] > maxX) maxX = pts[i][0];
        if (pts[i][1] < minY) minY = pts[i][1];
        if (pts[i][1] > maxY) maxY = pts[i][1];
    }
    var cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    var w = maxX - minX, h = maxY - minY;
    var r = Math.sqrt(w * w + h * h) / 2 + MARK_PADDING_MM * PT_PER_MM;
    var minR = MARK_MIN_RADIUS_MM * PT_PER_MM;
    if (r < minR) r = minR;

    var circle = g.pathItems.ellipse(cy + r, cx - r, 2 * r, 2 * r); // top, left, width, height
    circle.filled = false;
    circle.stroked = true;
    circle.strokeColor = yellow;
    circle.strokeWidth = 1;
    circle.strokeDashes = [4, 3];
    circle.name = "Overlap marker";
}

// ===================== Collecting paths =====================

function findPaths(collection, array) {
    for (var i = 0; i < collection.length; i++) {
        var item = collection[i];
        if (!isUsable(item)) continue;
        if (item.typename === "PathItem") {
            if (!item.guides) array.push(item);
        } else if (item.typename === "GroupItem") {
            findPaths(item.pageItems, array);
        } else if (item.typename === "CompoundPathItem") {
            findPaths(item.pathItems, array);
        }
    }
}

function isUsable(item) {
    try {
        if (item.locked || item.hidden) return false;
        if (item.layer) {
            if (item.layer.locked || !item.layer.visible) return false;
            if (item.layer.name === DUP_LAYER_NAME) return false; // Our own markers/duplicates
        }
    } catch (e) {
        return false;
    }
    return true;
}

// ===================== Cleaning =====================

function isSpeck(item) {
    if (Math.abs(item.area) >= SPECK_AREA) return false;
    var b = item.geometricBounds; // [left, top, right, bottom]
    var w = Math.abs(b[2] - b[0]);
    var h = Math.abs(b[1] - b[3]);
    return Math.sqrt(w * w + h * h) < SPECK_DIAG;
}

function samePoint(a, b) {
    return Math.abs(a[0] - b[0]) < POINT_MERGE_TOL && Math.abs(a[1] - b[1]) < POINT_MERGE_TOL;
}

function cleanPoints(path) {
    var removed = 0;
    var pts = path.pathPoints;
    if (pts.length < 2) return 0;

    for (var i = pts.length - 1; i > 0; i--) {
        if (samePoint(pts[i].anchor, pts[i - 1].anchor)) {
            pts[i - 1].rightDirection = pts[i].rightDirection;
            pts[i].remove();
            removed++;
        }
    }

    if (path.closed && pts.length > 2) {
        var last = pts[pts.length - 1];
        var first = pts[0];
        if (samePoint(last.anchor, first.anchor)) {
            first.leftDirection = last.leftDirection;
            last.remove();
            removed++;
        }
    }
    return removed;
}

// ===================== Geometry =====================

function dist(a, b) {
    var dx = a[0] - b[0], dy = a[1] - b[1];
    return Math.sqrt(dx * dx + dy * dy);
}

function flattenSegment(p0, p1, p2, p3, out) {
    var straight = dist(p0, p1) < 0.001 && dist(p2, p3) < 0.001;
    if (straight) { out.push(p3[0], p3[1]); return; }

    var est = dist(p0, p1) + dist(p1, p2) + dist(p2, p3);
    var steps = Math.ceil(est / CURVE_STEP);
    if (steps < 4) steps = 4;
    if (steps > 200) steps = 200;
    for (var i = 1; i <= steps; i++) {
        var t = i / steps, mt = 1 - t;
        var a = mt * mt * mt, b = 3 * mt * mt * t, c = 3 * mt * t * t, d = t * t * t;
        out.push(a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                 a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]);
    }
}

function buildInfo(path) {
    var pts, n, closed;
    try { pts = path.pathPoints; n = pts.length; closed = path.closed; } catch (e) { return null; }
    if (n < 2) return null;

    var poly = [];
    var a0 = pts[0].anchor;
    poly.push(a0[0], a0[1]);
    var segCount = closed ? n : n - 1;
    for (var s = 0; s < segCount; s++) {
        var P = pts[s], Q = pts[(s + 1) % n];
        flattenSegment(P.anchor, P.rightDirection, Q.leftDirection, Q.anchor, poly);
    }

    var minX = poly[0], maxX = poly[0], minY = poly[1], maxY = poly[1], length = 0;
    for (var i = 2; i < poly.length; i += 2) {
        var x = poly[i], y = poly[i + 1];
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        var dx = x - poly[i - 2], dy = y - poly[i - 1];
        length += Math.sqrt(dx * dx + dy * dy);
    }

    return {
        item: path, poly: poly, closed: closed,
        samples: makeSamples(poly, SAMPLE_STEP),
        fine: null, // fine checkpoints, made only when needed
        minX: minX, maxX: maxX, minY: minY, maxY: maxY,
        length: length, flagged: false
    };
}

// Checkpoints along the line: every corner, plus extra points every "step"
function makeSamples(poly, step) {
    var out = [];
    var m = poly.length / 2;
    for (var i = 0; i < m - 1; i++) {
        var x1 = poly[2 * i], y1 = poly[2 * i + 1], x2 = poly[2 * i + 2], y2 = poly[2 * i + 3];
        out.push(x1, y1);
        var L = Math.sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
        if (L > TOL) {
            var extra = Math.floor(L / step);
            if (extra < 1) extra = 1;
            for (var e = 1; e <= extra; e++) {
                var t = e / (extra + 1);
                out.push(x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
            }
        }
    }
    out.push(poly[poly.length - 2], poly[poly.length - 1]);
    return out;
}

function segDistSq(px, py, x1, y1, x2, y2) {
    var dx = x2 - x1, dy = y2 - y1, len2 = dx * dx + dy * dy, t = 0;
    if (len2 > 0) {
        t = ((px - x1) * dx + (py - y1) * dy) / len2;
        if (t < 0) t = 0; else if (t > 1) t = 1;
    }
    var cx = x1 + t * dx - px, cy = y1 + t * dy - py;
    return cx * cx + cy * cy;
}

function nearPoly(px, py, b) {
    if (px < b.minX - TOL || px > b.maxX + TOL || py < b.minY - TOL || py > b.maxY + TOL) return false;
    var p = b.poly, n = p.length / 2;
    for (var i = 0; i < n - 1; i++) {
        var x1 = p[2 * i], y1 = p[2 * i + 1], x2 = p[2 * i + 2], y2 = p[2 * i + 3];
        if (px < (x1 < x2 ? x1 : x2) - TOL || px > (x1 > x2 ? x1 : x2) + TOL ||
            py < (y1 < y2 ? y1 : y2) - TOL || py > (y1 > y2 ? y1 : y2) + TOL) continue;
        if (segDistSq(px, py, x1, y1, x2, y2) <= TOL_SQ) return true;
    }
    return false;
}

// Does line a lie entirely on top of line b (within tolerance)?
function isCoveredBy(a, b) {
    if (a.minX < b.minX - TOL || a.maxX > b.maxX + TOL ||
        a.minY < b.minY - TOL || a.maxY > b.maxY + TOL) return false;
    if (a.length > b.length + 2 * TOL) return false;

    var s = a.samples, m = s.length / 2;
    var quick = [0, m - 1, Math.floor(m / 2)];
    for (var q = 0; q < 3; q++) {
        if (!nearPoly(s[2 * quick[q]], s[2 * quick[q] + 1], b)) return false;
    }
    for (var i = 0; i < m; i++) {
        if (!nearPoly(s[2 * i], s[2 * i + 1], b)) return false;
    }
    return true;
}

// Finds stretches of line a (at least MIN_OVERLAP long) that lie on line b.
// Each stretch found is added to "out" as an array of [x, y] points.
function findPartialOverlaps(a, b, out) {
    // Boxes must touch at all
    if (a.minX > b.maxX + TOL || a.maxX < b.minX - TOL ||
        a.minY > b.maxY + TOL || a.maxY < b.minY - TOL) return;

    // Quick hint using the coarse checkpoints (spaced no wider than MIN_OVERLAP,
    // so any qualifying overlap contains at least one of them)
    var cs = a.samples, cm = cs.length / 2, hint = false;
    for (var c = 0; c < cm; c++) {
        if (nearPoly(cs[2 * c], cs[2 * c + 1], b)) { hint = true; break; }
    }
    if (!hint) return;

    // Fine trace
    if (!a.fine) a.fine = makeSamples(a.poly, FINE_STEP);
    var s = a.fine, m = s.length / 2;
    var runs = [], cur = null;
    for (var i = 0; i < m; i++) {
        if (nearPoly(s[2 * i], s[2 * i + 1], b)) {
            if (!cur) cur = { start: i, end: i, wrap: false };
            else cur.end = i;
        } else if (cur) {
            runs.push(cur);
            cur = null;
        }
    }
    if (cur) runs.push(cur);

    // Closed shape: a stretch crossing the start point is split in two — join it
    if (a.closed && runs.length > 1 && runs[0].start === 0 && runs[runs.length - 1].end === m - 1) {
        var lastRun = runs.pop();
        runs[0] = { start: lastRun.start, end: runs[0].end, wrap: true };
    }

    for (var r = 0; r < runs.length; r++) {
        var run = runs[r];
        var idx = [];
        if (run.wrap) {
            // Last checkpoint of a closed shape equals the first, so stop one early
            for (var w1 = run.start; w1 < m - 1; w1++) idx.push(w1);
            for (var w2 = 0; w2 <= run.end; w2++) idx.push(w2);
        } else {
            for (var w3 = run.start; w3 <= run.end; w3++) idx.push(w3);
        }
        if (idx.length < 2) continue;

        var pts = [], len = 0;
        for (var q = 0; q < idx.length; q++) {
            var x = s[2 * idx[q]], y = s[2 * idx[q] + 1];
            if (q > 0) {
                var dx = x - pts[q - 1][0], dy = y - pts[q - 1][1];
                len += Math.sqrt(dx * dx + dy * dy);
            }
            pts.push([x, y]);
        }
        if (len >= MIN_OVERLAP) out.push(pts);
    }
}

refinedJanitor();
