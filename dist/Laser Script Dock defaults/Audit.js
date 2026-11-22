// Adobe Illustrator Script: Master Laser Auditor (V3.6)
// Flags: improper colours, illegal fills, unsplit compounds, EMPTY compounds,
// INVISIBLE paths, and WRONG PINK/GREEN PAIRINGS.
//
// V3.5 changes:
//   - Pink/green pairing check, using the same inside/outside geometry as
//     Prep.js and Recolor.jsx (works on loose paths, not just compounds).
//     Every pink/green cut line gets a nesting depth = how many closed
//     cut shapes it sits inside. Then:
//       PINK  = even depth (0, 2, 4...) AND has a cut line inside it
//       GREEN = everything else (holes, lone shapes, open lines)
//     So: part = pink, its holes = green, a part inside a hole = pink if it
//     has holes of its own (otherwise green), its holes = green, and so on.
//   - Wrong pairings get a neon blue 2pt outline (fill untouched, so the
//     shapes inside stay visible) and go to the "WRONG PAIRING" layer,
//     which always sits above "NOT LASER COMPLIANT".
//   - Colour matching is exact: values are rounded to whole numbers, i.e.
//     exactly what the hex code shows (same as Prep.js). No tolerance.
//   - Fix: grayscale black now means 100% gray (it used to accept 0% = white).
// V3.6: Wrong-pairing highlight changed from deep orange to neon blue (0, 255, 255).
// V3.4 behaviour retained:
//   - Non-compliant items: yellow fill + stroke, moved to "NOT LASER COMPLIANT".
//   - Empty compounds and ghost paths (no fill, no stroke) are flagged.
//   - Moving happens AFTER the audit pass so groups aren't changed mid-loop.
//   - Selection-only scope. Text frames, rasters, placed items are ignored.
//
// NOTE: the pairing check only "sees" what's selected. Select the whole part
// (outer cut + all its holes), or the check can't know what's inside what.

function masterAuditor() {
    if (app.documents.length === 0) return;
    var doc = app.activeDocument;
    var sel = doc.selection;

    if (!sel || sel.length === 0) {
        alert("Please select artwork first!");
        return;
    }

    // --- Settings ---
    var SHAME_LAYER_NAME   = "NOT LASER COMPLIANT";
    var PAIRING_LAYER_NAME = "WRONG PAIRING";
    var PT_PER_MM  = 2.834645669;
    var EDGE_TOL   = 0.05 * PT_PER_MM;  // points this close to an outline count as "on" it
    var CURVE_STEP = 0.25 * PT_PER_MM;  // how finely curves are measured
    var PAIRING_STROKE_WIDTH = 2;

    var yellowHighlight = mkRGB(255, 255, 0);
    var neonBlueHighlight = mkRGB(0, 255, 255);  // neon blue (#00FFFF)

    var flaggedItems = []; // not laser compliant
    var cutLines     = []; // every pink/green path: {item, info, isPink, judge}
    var wrongPairs   = []; // compliant pink/green paths with the wrong colour

    // Snapshot the selection
    var items = [];
    for (var s = 0; s < sel.length; s++) items.push(sel[s]);

    for (var i = 0; i < items.length; i++) {
        checkItem(items[i]);
    }

    checkPairings();

    // =========================================================================

    function mkRGB(r, g, b) {
        var c = new RGBColor(); c.red = r; c.green = g; c.blue = b; return c;
    }

    function checkItem(item) {
        if (item.typename === "GroupItem") {
            for (var g = 0; g < item.pageItems.length; g++) checkItem(item.pageItems[g]);
            return;
        }

        var isIllegal = false;

        // --- GHOST PATH CHECK ---
        // If it has no stroke AND no fill, it's a ghost path.
        if (item.typename === "PathItem") {
            if (!item.stroked && !item.filled) {
                isIllegal = true;
            }
        }

        // --- LOGIC A: COMPOUND PATHS ---
        if (item.typename === "CompoundPathItem") {
            if (item.pathItems.length === 0) {
                // Empty compound path: shouldn't be compound, shouldn't exist. Flag it.
                isIllegal = true;
            } else {
                var cp = item.pathItems[0];
                var isLaserColor = (isRed(cp.strokeColor) || isBlue(cp.strokeColor) || isBlack(cp.strokeColor) || !cp.stroked);
                var isLegalFillC = !cp.filled || isBlack(cp.fillColor);

                // Flag if it's Pink/Green (needs splitting) OR if it has a non-black fill
                if (!isLaserColor || !isLegalFillC) {
                    isIllegal = true;
                }

                // Pink/green sub-paths still count as shapes for the pairing
                // check (something can be inside them), but they aren't judged
                // themselves - the compound is already flagged as needing a split.
                for (var q = 0; q < item.pathItems.length; q++) {
                    var sp = item.pathItems[q];
                    if (sp.stroked && (isPink(sp.strokeColor) || isGreen(sp.strokeColor))) {
                        registerCutLine(sp, isPink(sp.strokeColor), false);
                    }
                }
            }
        }

        // --- LOGIC B: SIMPLE PATHS ---
        else if (item.typename === "PathItem") {
            var st = item.strokeColor;
            var f = item.fillColor;

            // Stroke must be Red, Blue, Black, Pink, Green, or absent.
            var isLegalStroke = isRed(st) || isBlue(st) || isBlack(st) || isPink(st) || isGreen(st) || !item.stroked;
            // Fill must be Black or absent (also catches stroke + non-black fill).
            var isLegalFill = isBlack(f) || !item.filled;

            if (!isLegalStroke || !isLegalFill) {
                isIllegal = true;
            }

            // Register pink/green lines for the pairing check. Only lines
            // that passed the compliance check get judged for pairing.
            if (item.stroked && (isPink(st) || isGreen(st))) {
                registerCutLine(item, isPink(st), !isIllegal);
            }
        }

        // Text frames, raster images, and placed items are intentionally ignored.

        if (isIllegal) {
            flagItem(item);
            flaggedItems.push(item);
        }
    }

    function registerCutLine(path, pink, judge) {
        var info = buildInfo(path);
        if (!info) return;
        cutLines.push({ item: path, info: info, isPink: pink, judge: judge });
    }

    // --- PAIRING CHECK -------------------------------------------------------

    function checkPairings() {
        for (var a = 0; a < cutLines.length; a++) {
            var c = cutLines[a];
            if (!c.judge) continue;

            var depth = 0;          // how many closed cut shapes contain this one
            var hasInside = false;  // is any cut line inside this one?

            for (var b = 0; b < cutLines.length; b++) {
                if (a === b) continue;
                var o = cutLines[b];
                if (isInside(c.info, o.info)) depth++;
                else if (!hasInside && isInside(o.info, c.info)) hasInside = true;
            }

            var shouldBePink = (depth % 2 === 0) && hasInside;
            if (shouldBePink !== c.isPink) wrongPairs.push(c.item);
        }
    }

    // --- FLAGGING ------------------------------------------------------------

    function applyYellowFlag(path) {
        var hadStroke = path.stroked;

        path.filled = true;
        path.fillColor = yellowHighlight;

        path.stroked = true;
        path.strokeColor = yellowHighlight;

        // If the path originally had no stroke, give it a default 1pt width
        if (!hadStroke) {
            path.strokeWidth = 1;
        }
    }

    function flagItem(item) {
        if (item.typename === "CompoundPathItem") {
            // Empty compounds simply loop zero times: counted, but nothing to paint.
            for (var p = 0; p < item.pathItems.length; p++) {
                applyYellowFlag(item.pathItems[p]);
            }
        } else {
            applyYellowFlag(item);
        }
    }

    function applyNeonBlueFlag(path) {
        // Outline only - a fill would cover the holes sitting inside it
        path.stroked = true;
        path.strokeColor = neonBlueHighlight;
        path.strokeWidth = PAIRING_STROKE_WIDTH;
    }

    // --- LAYERS --------------------------------------------------------------

    function findLayer(name) {
        try { return doc.layers.getByName(name); } catch (e) { return null; }
    }

    function getLayer(name) {
        var layer = findLayer(name);
        if (!layer) {
            layer = doc.layers.add();
            layer.name = name;
        }
        // Make sure the script can actually move things onto it
        layer.visible = true;
        layer.locked = false;
        return layer;
    }

    function bringToTop(layer) {
        try { layer.zOrder(ZOrderMethod.BRINGTOFRONT); }
        catch (e) {
            try { layer.move(doc, ElementPlacement.PLACEATBEGINNING); } catch (e2) { }
        }
    }

    // --- REPORT --------------------------------------------------------------

    var nShame = flaggedItems.length;
    var nPair  = wrongPairs.length;

    if (nShame === 0 && nPair === 0) {
        alert("The selected artwork is laser compliant.");
        return;
    }

    // Move offenders AFTER the audit pass, so we never mutate a group's
    // pageItems collection while still iterating over it.
    if (nShame > 0) {
        var shameLayer = getLayer(SHAME_LAYER_NAME);
        bringToTop(shameLayer);
        for (var m = 0; m < flaggedItems.length; m++) {
            flaggedItems[m].move(shameLayer, ElementPlacement.PLACEATBEGINNING);
        }
    }

    if (nPair > 0) {
        var pairLayer = getLayer(PAIRING_LAYER_NAME);
        for (var w = 0; w < wrongPairs.length; w++) {
            applyNeonBlueFlag(wrongPairs[w]);
            wrongPairs[w].move(pairLayer, ElementPlacement.PLACEATBEGINNING);
        }
    }

    // "WRONG PAIRING" always sits above "NOT LASER COMPLIANT"
    // (also when it's left over from an earlier run)
    var existingPairLayer = findLayer(PAIRING_LAYER_NAME);
    if (existingPairLayer) bringToTop(existingPairLayer);

    app.redraw();

    var msg = "The audit found some problems:\n";
    if (nShame > 0) {
        msg += "\n- " + nShame + " item(s) weren't laser compliant. They were marked yellow and moved to the '" + SHAME_LAYER_NAME + "' layer.";
    }
    if (nPair > 0) {
        msg += "\n- " + nPair + " cut line(s) had the wrong pink/green colour for where they sit. They were outlined neon blue and moved to the '" + PAIRING_LAYER_NAME + "' layer.";
    }
    alert(msg);

    // --- GEOMETRY: what's inside what (identical to Prep.js / Recolor.jsx) ----

    function dist(a, b) {
        var dx = a[0] - b[0], dy = a[1] - b[1];
        return Math.sqrt(dx * dx + dy * dy);
    }

    // Turns one curve segment into a chain of short straight pieces
    function flattenSegment(p0, p1, p2, p3, out) {
        if (dist(p0, p1) < 0.001 && dist(p2, p3) < 0.001) { out.push(p3[0], p3[1]); return; }
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

        var minX = poly[0], maxX = poly[0], minY = poly[1], maxY = poly[1];
        for (var i = 2; i < poly.length; i += 2) {
            var x = poly[i], y = poly[i + 1];
            if (x < minX) minX = x; if (x > maxX) maxX = x;
            if (y < minY) minY = y; if (y > maxY) maxY = y;
        }

        // Enclosed area (shoelace formula); open lines enclose nothing
        var area = 0;
        if (closed) {
            var m = poly.length / 2;
            for (var j = 0; j < m; j++) {
                var k = (j + 1) % m;
                area += poly[2 * j] * poly[2 * k + 1] - poly[2 * k] * poly[2 * j + 1];
            }
            area = Math.abs(area / 2);
        }

        return { poly: poly, closed: closed, area: area,
                 minX: minX, maxX: maxX, minY: minY, maxY: maxY };
    }

    function pointInPoly(x, y, poly) {
        var inside = false, n = poly.length / 2;
        for (var i = 0, j = n - 1; i < n; j = i++) {
            var xi = poly[2 * i], yi = poly[2 * i + 1], xj = poly[2 * j], yj = poly[2 * j + 1];
            if (((yi > y) !== (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) inside = !inside;
        }
        return inside;
    }

    function nearEdge(x, y, poly) {
        var n = poly.length / 2, tolSq = EDGE_TOL * EDGE_TOL;
        for (var i = 0; i < n - 1; i++) {
            var x1 = poly[2 * i], y1 = poly[2 * i + 1], x2 = poly[2 * i + 2], y2 = poly[2 * i + 3];
            var dx = x2 - x1, dy = y2 - y1, len2 = dx * dx + dy * dy, t = 0;
            if (len2 > 0) {
                t = ((x - x1) * dx + (y - y1) * dy) / len2;
                if (t < 0) t = 0; else if (t > 1) t = 1;
            }
            var cx = x1 + t * dx - x, cy = y1 + t * dy - y;
            if (cx * cx + cy * cy <= tolSq) return true;
        }
        return false;
    }

    // Is shape a completely inside closed shape b?
    function isInside(a, b) {
        if (!a || !b || a === b || !b.closed) return false;
        if (a.minX < b.minX - EDGE_TOL || a.maxX > b.maxX + EDGE_TOL ||
            a.minY < b.minY - EDGE_TOL || a.maxY > b.maxY + EDGE_TOL) return false;
        if (b.area <= a.area) return false; // The container must be the bigger shape

        var p = a.poly, n = p.length / 2;
        var stride = Math.max(1, Math.floor(n / 200)); // Cap the work on very detailed shapes
        for (var i = 0; i < n; i += stride) {
            var x = p[2 * i], y = p[2 * i + 1];
            if (!pointInPoly(x, y, b.poly) && !nearEdge(x, y, b.poly)) return false;
        }
        return true;
    }
}

// Colour matchers (RGB 0-255) -- strict pass/fail, exactly what the laser expects.
// Values are rounded to whole numbers, i.e. exactly what the hex code shows.
// This absorbs invisible floating-point leftovers from Pathfinder (0.0000001),
// but 254 is NOT 255 - no tolerance.
function rgbMatch(c, r, g, b) {
    return !!c && c.typename === "RGBColor" &&
        Math.round(c.red) === r &&
        Math.round(c.green) === g &&
        Math.round(c.blue) === b;
}
function isRed(c)   { return rgbMatch(c, 255, 0, 0); }
function isBlue(c)  { return rgbMatch(c, 0, 0, 255); }
function isPink(c)  { return rgbMatch(c, 255, 0, 255); }
function isGreen(c) { return rgbMatch(c, 0, 255, 0); }
// Black also accepts a pure grayscale swatch at 100% gray (= #000).
function isBlack(c) {
    if (!c) return false;
    if (c.typename === "GrayColor") return Math.round(c.gray) === 100;
    return rgbMatch(c, 0, 0, 0);
}

masterAuditor();
