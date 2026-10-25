#include "video/VideoKeyframeEval.h"

#include "audio/FadeCurves.h"

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

FadeCurveType mapVideoEase(VideoKeyframeType type)
{
    switch (type) {
    case VideoKeyframeType::Slow:
        return FadeCurveType::Slow;
    case VideoKeyframeType::Fast:
        return FadeCurveType::Fast;
    case VideoKeyframeType::Smooth:
        return FadeCurveType::Smooth;
    case VideoKeyframeType::Sharp:
        return FadeCurveType::Sharp;
    case VideoKeyframeType::Linear:
    case VideoKeyframeType::Hold:
    default:
        return FadeCurveType::Linear;
    }
}

template <typename T>
double segmentT(const T &a, const T &b, double timeSec)
{
    const double dt = b.timeSec - a.timeSec;
    if (dt <= 1e-12) {
        return 1.0;
    }
    return std::clamp((timeSec - a.timeSec) / dt, 0.0, 1.0);
}

double lerpD(double a, double b, double t)
{
    return a + (b - a) * t;
}

PanCropKeyframe lerpPanCrop(const PanCropKeyframe &a, const PanCropKeyframe &b, double t)
{
    PanCropKeyframe o;
    o.timeSec = lerpD(a.timeSec, b.timeSec, t);
    o.width = lerpD(a.width, b.width, t);
    o.height = lerpD(a.height, b.height, t);
    o.xCenter = lerpD(a.xCenter, b.xCenter, t);
    o.yCenter = lerpD(a.yCenter, b.yCenter, t);
    o.angleDeg = lerpD(a.angleDeg, b.angleDeg, t);
    o.rotationXCenter = lerpD(a.rotationXCenter, b.rotationXCenter, t);
    o.rotationYCenter = lerpD(a.rotationYCenter, b.rotationYCenter, t);
    o.smoothness = lerpD(a.smoothness, b.smoothness, t);
    o.type = a.type;
    return o;
}

TrackMotionKeyframe lerpMotion(const TrackMotionKeyframe &a, const TrackMotionKeyframe &b, double t)
{
    TrackMotionKeyframe o;
    o.timeSec = lerpD(a.timeSec, b.timeSec, t);
    o.positionX = lerpD(a.positionX, b.positionX, t);
    o.positionY = lerpD(a.positionY, b.positionY, t);
    o.width = lerpD(a.width, b.width, t);
    o.height = lerpD(a.height, b.height, t);
    o.rotationZ = lerpD(a.rotationZ, b.rotationZ, t);
    o.orientationZ = lerpD(a.orientationZ, b.orientationZ, t);
    o.smoothness = lerpD(a.smoothness, b.smoothness, t);
    o.type = a.type;
    return o;
}

} // namespace

double videoKeyframeEase(VideoKeyframeType type, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    if (type == VideoKeyframeType::Hold) {
        return 0.0;
    }
    if (type == VideoKeyframeType::Linear) {
        return t;
    }
    return fadeCurveAmplitude(mapVideoEase(type), t);
}

VideoKeyframeType videoKeyframeTypeFromVegasCode(int code)
{
    switch (code) {
    case 1:
        return VideoKeyframeType::Fast;
    case 2:
        return VideoKeyframeType::Slow;
    case 3:
        return VideoKeyframeType::Smooth;
    case 4:
        return VideoKeyframeType::Sharp;
    case 5:
        return VideoKeyframeType::Hold;
    default:
        return VideoKeyframeType::Linear;
    }
}

int videoKeyframeTypeToVegasCode(VideoKeyframeType type)
{
    switch (type) {
    case VideoKeyframeType::Fast:
        return 1;
    case VideoKeyframeType::Slow:
        return 2;
    case VideoKeyframeType::Smooth:
        return 3;
    case VideoKeyframeType::Sharp:
        return 4;
    case VideoKeyframeType::Hold:
        return 5;
    case VideoKeyframeType::Linear:
    default:
        return 0;
    }
}

QString videoKeyframeTypeName(VideoKeyframeType type)
{
    switch (type) {
    case VideoKeyframeType::Fast:
        return QStringLiteral("Fast");
    case VideoKeyframeType::Slow:
        return QStringLiteral("Slow");
    case VideoKeyframeType::Smooth:
        return QStringLiteral("Smooth");
    case VideoKeyframeType::Sharp:
        return QStringLiteral("Sharp");
    case VideoKeyframeType::Hold:
        return QStringLiteral("Hold");
    case VideoKeyframeType::Linear:
    default:
        return QStringLiteral("Linear");
    }
}

PanCropKeyframe evaluatePanCrop(const EventPanCropState &state, double localTimeSec, int frameW,
                                int frameH)
{
    EventPanCropState st = state;
    st.ensureDefault(frameW, frameH);
    const auto &kfs = st.positionKeyframes;
    if (kfs.size() == 1) {
        return kfs.first();
    }
    int i1 = 0;
    for (int i = 0; i < kfs.size(); ++i) {
        if (kfs[i].timeSec <= localTimeSec + 1e-9) {
            i1 = i;
        }
    }
    if (i1 >= kfs.size() - 1) {
        return kfs.last();
    }
    const PanCropKeyframe &a = kfs[i1];
    const PanCropKeyframe &b = kfs[i1 + 1];
    if (a.type == VideoKeyframeType::Hold) {
        return a;
    }
    const double u = videoKeyframeEase(a.type, segmentT(a, b, localTimeSec));
    return lerpPanCrop(a, b, u);
}

TrackMotionKeyframe evaluateTrackMotion(const TrackMotionState &state, double timeSec,
                                        double aspect)
{
    TrackMotionState st = state;
    st.ensureDefault(aspect);
    const auto &kfs = st.motionKeyframes;
    if (kfs.size() == 1) {
        return kfs.first();
    }
    int i1 = 0;
    for (int i = 0; i < kfs.size(); ++i) {
        if (kfs[i].timeSec <= timeSec + 1e-9) {
            i1 = i;
        }
    }
    if (i1 >= kfs.size() - 1) {
        return kfs.last();
    }
    const TrackMotionKeyframe &a = kfs[i1];
    const TrackMotionKeyframe &b = kfs[i1 + 1];
    if (a.type == VideoKeyframeType::Hold) {
        return a;
    }
    const double u = videoKeyframeEase(a.type, segmentT(a, b, timeSec));
    return lerpMotion(a, b, u);
}

const MaskKeyframe *maskHoldAt(const EventPanCropState &state, double localTimeSec)
{
    const int idx = state.maskIndexAt(localTimeSec);
    if (idx < 0 || idx >= state.maskKeyframes.size()) {
        return nullptr;
    }
    return &state.maskKeyframes[idx];
}

namespace {

// Mask morphing, after VEGAS's own (sharedk.dll: Mask::Interpolate, Path::Interpolate and
// the anchor blend it calls, FUN_18020b960). See MARKDOWN/PANCROP_MASK_MORPH.md.

/**
 * Below this (per coordinate, in frame pixels) a handle is "on" its anchor and has no
 * direction worth turning — the same 0.001 VEGAS compares against everywhere in the blend.
 */
constexpr double kHandleEps = 0.001;
constexpr double kPi = 3.14159265358979323846;

bool onAnchor(double hx, double hy)
{
    return std::abs(hx) < kHandleEps && std::abs(hy) < kHandleEps;
}

/** The handle direction pointing the other way, kept inside (-pi, pi]. */
double opposite(double angle)
{
    return angle > 0.0 ? angle - kPi : angle + kPi;
}

/**
 * One anchor in between two keyframes — FUN_18020b960 line for line. VEGAS turns a handle
 * rather than sliding its tip: angle (the shorter way round) and length are interpolated
 * separately, so a curve keeps its roundness through the move instead of flattening
 * halfway. Angles are those of anchor-minus-handle, as VEGAS measures them.
 *
 * The rules that make this more than a polar lerp:
 *  - a handle on its anchor has no angle of its own and takes the opposite of the other
 *    handle's, so it grows out in line with it; its length starts from zero;
 *  - a smooth or symmetric anchor's out handle follows its in handle (opposite angle,
 *    own length), whatever direction it was stored at;
 *  - against a corner, or when both sides are on their anchor, or when both angles agree,
 *    the tips just slide.
 */
MaskAnchor blendAnchor(const MaskAnchor &a, const MaskAnchor &b, double t)
{
    if (t <= 0.0) {
        return a;
    }
    if (t >= 1.0) {
        return b;
    }
    MaskAnchor o;
    o.id = a.id;
    o.x = lerpD(a.x, b.x, t);
    o.y = lerpD(a.y, b.y, t);
    auto lerpIn = [&] {
        o.inX = lerpD(a.inX, b.inX, t);
        o.inY = lerpD(a.inY, b.inY, t);
    };
    auto lerpOut = [&] {
        o.outX = lerpD(a.outX, b.outX, t);
        o.outY = lerpD(a.outY, b.outY, t);
    };

    // Which handles sit on their anchor, and the kind each side counts as: none when VEGAS
    // says so or when both its handles are on the anchor.
    bool aInOn = true, aOutOn = true, bInOn = true, bOutOn = true;
    int ta = -1, tb = -1;
    if (a.type != -1) {
        aInOn = onAnchor(a.inX, a.inY);
        aOutOn = onAnchor(a.outX, a.outY);
        ta = (aInOn && aOutOn) ? -1 : a.type;
    }
    if (b.type != -1) {
        bInOn = onAnchor(b.inX, b.inY);
        bOutOn = onAnchor(b.outX, b.outY);
        tb = (bInOn && bOutOn) ? -1 : b.type;
    }
    if (ta == -1 || tb == -1) {
        // One side has no handles: the other's kind, handles sliding into place.
        o.type = ta == -1 ? tb : ta;
        if (o.type != -1) {
            lerpIn();
            lerpOut();
        }
        return o; // with no kind on either side the handles stay on the anchor
    }
    o.type = (ta == 0 || tb == 0) ? 0 : (ta == 2 && tb == 2) ? 2 : 1;

    const bool inSlides = (ta == 0 && bInOn) || (tb == 0 && aInOn) || (aInOn && bInOn);
    const bool outSlides = (ta == 0 && bOutOn) || (tb == 0 && aOutOn) || (aOutOn && bOutOn);
    auto angleOf = [](double hx, double hy) { return std::atan2(-hy, -hx); };
    double angAIn = 0.0, lenAIn = 0.0, angBIn = 0.0, lenBIn = 0.0;
    double angAOut = 0.0, lenAOut = 0.0, angBOut = 0.0, lenBOut = 0.0;
    if (!inSlides) {
        if (!aInOn) {
            angAIn = angleOf(a.inX, a.inY);
            lenAIn = std::hypot(a.inX, a.inY);
        }
        if (!bInOn) {
            angBIn = angleOf(b.inX, b.inY);
            lenBIn = std::hypot(b.inX, b.inY);
        }
    }
    if (!outSlides) {
        if (!inSlides && !aInOn && ta != 0) {
            angAOut = opposite(angAIn);
            lenAOut = std::hypot(a.outX, a.outY);
        } else if (!aOutOn) {
            angAOut = angleOf(a.outX, a.outY);
            lenAOut = std::hypot(a.outX, a.outY);
            if (!inSlides && aInOn) {
                angAIn = opposite(angAOut);
            }
        }
        if (!inSlides && !bInOn && tb != 0) {
            angBOut = opposite(angBIn);
            lenBOut = std::hypot(b.outX, b.outY);
        } else if (!bOutOn) {
            angBOut = angleOf(b.outX, b.outY);
            lenBOut = std::hypot(b.outX, b.outY);
            if (!inSlides && bInOn) {
                angBIn = opposite(angBOut);
            }
        }
    }

    auto turn = [t](double from, double to) {
        double d = to - from;
        if (d > kPi) {
            d -= 2.0 * kPi;
        } else if (d < -kPi) {
            d += 2.0 * kPi;
        }
        return from + d * t;
    };
    bool inTurned = false;
    double inCos = 0.0, inSin = 0.0;
    if (inSlides || std::abs(angAIn - angBIn) < kHandleEps) {
        lerpIn();
    } else {
        const double ang = turn(angAIn, angBIn);
        const double len = lerpD(lenAIn, lenBIn, t);
        inCos = std::cos(ang);
        inSin = std::sin(ang);
        o.inX = -len * inCos;
        o.inY = -len * inSin;
        inTurned = true;
    }
    if (outSlides || std::abs(angAOut - angBOut) < kHandleEps) {
        lerpOut();
    } else if (o.type == 2) {
        o.outX = -o.inX; // symmetric: the out handle is the in handle mirrored
        o.outY = -o.inY;
    } else {
        const double len = lerpD(lenAOut, lenBOut, t);
        if (o.type == 1 && inTurned) {
            o.outX = len * inCos; // smooth: straight on from the in handle
            o.outY = len * inSin;
        } else {
            const double ang = turn(angAOut, angBOut);
            o.outX = -len * std::cos(ang);
            o.outY = -len * std::sin(ang);
        }
    }
    return o;
}

/**
 * Split the curve from `left` to `right` at Bezier parameters `params` (ascending, inside
 * 0..1) by De Casteljau. Returns the new anchors in order; `left`'s out handle and
 * `right`'s in handle are shortened to match, so the curve is the same curve, now with
 * more anchors on it. Like VEGAS this works from the right end back, cutting what is left
 * of the curve each time, so a parameter of 1 is just the right end.
 */
QVector<MaskAnchor> splitSegment(MaskAnchor *left, MaskAnchor *right, const QVector<double> &params)
{
    QVector<MaskAnchor> out(params.size());
    const double p0x = left->x, p0y = left->y;
    double p1x = left->x + left->outX, p1y = left->y + left->outY;
    double p2x = right->x + right->inX, p2y = right->y + right->inY;
    double p3x = right->x, p3y = right->y;
    MaskAnchor *next = right; // the anchor whose in handle the next cut sets
    double whole = 1.0;       // where the curve still being cut ends, in the first's terms
    for (int i = int(params.size()) - 1; i >= 0; --i) {
        const double s = whole > 0.0 ? std::clamp(params[i] / whole, 0.0, 1.0) : 0.0;
        const double q0x = lerpD(p0x, p1x, s), q0y = lerpD(p0y, p1y, s);
        const double q1x = lerpD(p1x, p2x, s), q1y = lerpD(p1y, p2y, s);
        const double q2x = lerpD(p2x, p3x, s), q2y = lerpD(p2y, p3y, s);
        const double r0x = lerpD(q0x, q1x, s), r0y = lerpD(q0y, q1y, s);
        const double r1x = lerpD(q1x, q2x, s), r1y = lerpD(q1y, q2y, s);
        const double mx = lerpD(r0x, r1x, s), my = lerpD(r0y, r1y, s);
        next->inX = q2x - next->x;
        next->inY = q2y - next->y;
        MaskAnchor &m = out[i];
        m.x = mx;
        m.y = my;
        m.inX = r0x - mx;
        m.inY = r0y - my;
        m.outX = r1x - mx;
        m.outY = r1y - my;
        m.type = 1; // NewAnchorAfter(..., 1, ...): a smooth anchor
        next = &m;
        p1x = q0x;
        p1y = q0y;
        p2x = r0x;
        p2y = r0y;
        p3x = mx;
        p3y = my;
        whole = params[i];
    }
    if (!params.isEmpty()) {
        left->outX = p1x - left->x;
        left->outY = p1y - left->y;
    }
    return out;
}

/**
 * Where along the polyline left → middle… → right each middle point falls, as a share of
 * its length — the Bezier parameters VEGAS splits the other keyframe's segment at. A
 * polyline shorter than 0.001 is measured against 1 instead, as there, which puts every
 * point at the right end.
 */
QVector<double> chordFractions(const MaskAnchor &left, const QVector<MaskAnchor> &middle,
                               const MaskAnchor &right)
{
    QVector<double> cumulative;
    double total = 0.0;
    double px = left.x, py = left.y;
    for (const MaskAnchor &m : middle) {
        total += std::hypot(m.x - px, m.y - py);
        cumulative.push_back(total);
        px = m.x;
        py = m.y;
    }
    total += std::hypot(right.x - px, right.y - py);
    const double scale = total < kHandleEps ? 1.0 : total;
    QVector<double> f;
    for (double c : cumulative) {
        f.push_back(1.0 - (total - c) / scale);
    }
    return f;
}

/** An anchor's identity for pairing: its id, or its place when the contour has none. */
QVector<qint64> anchorKeys(const MaskPath &p)
{
    QVector<qint64> keys;
    bool haveIds = !p.anchors.isEmpty();
    for (const MaskAnchor &a : p.anchors) {
        haveIds = haveIds && a.id >= 0;
    }
    for (int i = 0; i < p.anchors.size(); ++i) {
        keys.push_back(haveIds ? p.anchors[i].id : i);
    }
    return keys;
}

/**
 * Two contours as equally long anchor lists that correspond one to one (Path::Interpolate).
 * Anchors pair by id. Between two paired anchors, runs of the same length pair in order;
 * otherwise pairs are taken from both ends of the runs, and what is left on one side —
 * anchors that one keyframe has and the other does not — gets partners made by splitting
 * the other keyframe's segment between the same two anchors, at the shares of length the
 * extra anchors sit at. So an added anchor grows out of the curve it was added to, and a
 * removed one flattens back into the curve it leaves.
 *
 * How the ends are taken matters to the shape halfway: half the shorter run from each
 * end, and an odd one left over pairs towards whichever end it sits nearer to.
 */
void correspond(const MaskPath &a, const MaskPath &b, QVector<MaskAnchor> *outA,
                QVector<MaskAnchor> *outB)
{
    QVector<MaskAnchor> A = a.anchors;
    QVector<MaskAnchor> B = b.anchors;
    const int na = int(A.size()), nb = int(B.size());
    const QVector<qint64> ka = anchorKeys(a);
    const QVector<qint64> kb = anchorKeys(b);
    QVector<int> matchA, matchB; // paired anchors, in a's order
    for (int i = 0; i < na; ++i) {
        const int j = int(kb.indexOf(ka[i]));
        if (j >= 0) {
            matchA.push_back(i);
            matchB.push_back(j);
        }
    }
    auto bare = [](MaskAnchor at) {
        at.inX = at.inY = at.outX = at.outY = 0.0;
        at.type = -1;
        return at;
    };
    const bool closed = a.closed && b.closed;
    if (matchA.isEmpty()) {
        // Nothing in common: pair in order. The longer one's extra anchors meet the shorter
        // one's last anchor — or, on a closed contour, where its walk wraps round to: the
        // first.
        const int n = std::min(na, nb);
        const int meet = closed ? 0 : std::max(0, n - 1);
        for (int i = 0; i < std::max(na, nb); ++i) {
            outA->push_back(i < na ? A[i] : bare(A[meet]));
            outB->push_back(i < nb ? B[i] : bare(B[meet]));
        }
        return;
    }

    struct Slot {
        int ia = -1, ib = -1; // anchor indices, or -1 for a made-up partner
        MaskAnchor sa, sb;
    };
    auto run = [](int from, int to, int n, bool closed) {
        QVector<int> r;
        if (closed) {
            for (int i = (from + 1) % n; i != to; i = (i + 1) % n) {
                r.push_back(i);
            }
        } else {
            for (int i = from + 1; i < to; ++i) {
                r.push_back(i);
            }
        }
        return r;
    };
    // Pair two runs. Equal runs pair in order; otherwise half the shorter run from each end,
    // an odd one left over towards the end it is nearer to (measured in its own keyframe),
    // then the rest of the longer side gets partners split out of the other side's segment
    // between (leftA, leftB) and (rightA, rightB).
    auto dist2 = [](const MaskAnchor &p, const MaskAnchor &q) {
        return (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y);
    };
    auto pairRuns = [&](QVector<int> ra, QVector<int> rb, int leftA, int leftB, int rightA,
                        int rightB) {
        QVector<Slot> head, mid, tail;
        if (ra.size() != rb.size()) {
            const int half = int(std::min(ra.size(), rb.size())) / 2;
            for (int i = 0; i < half; ++i) {
                head.push_back({ra.takeFirst(), rb.takeFirst(), {}, {}});
                tail.prepend({ra.takeLast(), rb.takeLast(), {}, {}});
            }
            if (!ra.isEmpty() && !rb.isEmpty()) {
                const bool fromA = ra.size() == 1;
                const QVector<MaskAnchor> &side = fromA ? A : B;
                const int lone = fromA ? ra.first() : rb.first();
                const int l = head.isEmpty() ? (fromA ? leftA : leftB)
                                             : (fromA ? head.last().ia : head.last().ib);
                const int r = tail.isEmpty() ? (fromA ? rightA : rightB)
                                             : (fromA ? tail.first().ia : tail.first().ib);
                if (dist2(side[lone], side[r]) < dist2(side[lone], side[l])) {
                    tail.prepend({ra.takeLast(), rb.takeLast(), {}, {}});
                } else {
                    head.push_back({ra.takeFirst(), rb.takeFirst(), {}, {}});
                }
            }
        }
        if (ra.size() == rb.size()) {
            for (int i = 0; i < ra.size(); ++i) {
                mid.push_back({ra[i], rb[i], {}, {}});
            }
        } else {
            const int lA = head.isEmpty() ? leftA : head.last().ia;
            const int lB = head.isEmpty() ? leftB : head.last().ib;
            const int rA = tail.isEmpty() ? rightA : tail.first().ia;
            const int rB = tail.isEmpty() ? rightB : tail.first().ib;
            if (rb.isEmpty()) {
                QVector<MaskAnchor> extra;
                for (int i : ra) extra.push_back(A[i]);
                const QVector<MaskAnchor> made =
                    (lB >= 0 && rB >= 0) ? splitSegment(&B[lB], &B[rB], chordFractions(A[lA], extra, A[rA]))
                                         : QVector<MaskAnchor>();
                for (int i = 0; i < ra.size(); ++i) {
                    Slot s;
                    s.ia = ra[i];
                    s.sb = i < made.size() ? made[i] : bare(B[lB >= 0 ? lB : rB]);
                    mid.push_back(s);
                }
            } else {
                QVector<MaskAnchor> extra;
                for (int i : rb) extra.push_back(B[i]);
                const QVector<MaskAnchor> made =
                    (lA >= 0 && rA >= 0) ? splitSegment(&A[lA], &A[rA], chordFractions(B[lB], extra, B[rB]))
                                         : QVector<MaskAnchor>();
                for (int i = 0; i < rb.size(); ++i) {
                    Slot s;
                    s.ib = rb[i];
                    s.sa = i < made.size() ? made[i] : bare(A[lA >= 0 ? lA : rA]);
                    mid.push_back(s);
                }
            }
        }
        return head + mid + tail;
    };

    const int k = int(matchA.size());
    const int gaps = closed ? k : k - 1;
    QVector<QVector<Slot>> gapSlots(k);
    // VEGAS blends a closed contour's first paired anchor last, from a copy taken before any
    // run was split: a split in the run that closes the contour shortens that anchor's in
    // handle, and the copy does not see it. The handle stays full length, which is the
    // fuller corner its render shows where the ellipse in the reference project loses and
    // regains an anchor. Its out handle, shortened by the first run, is carried over.
    const MaskAnchor firstA = A[matchA.first()];
    const MaskAnchor firstB = B[matchB.first()];
    for (int g = 0; g < gaps; ++g) {
        const int g2 = (g + 1) % k;
        gapSlots[g] = pairRuns(run(matchA[g], matchA[g2], na, closed),
                               run(matchB[g], matchB[g2], nb, closed), matchA[g], matchB[g],
                               matchA[g2], matchB[g2]);
    }
    if (closed) {
        A[matchA.first()].inX = firstA.inX;
        A[matchA.first()].inY = firstA.inY;
        B[matchB.first()].inX = firstB.inX;
        B[matchB.first()].inY = firstB.inY;
    }
    // An open contour's anchors before its first pair and after its last have only one
    // neighbour. They pair outwards from it; the longer side's farthest extras meet the
    // shorter side's outermost anchor (the neighbour itself when it has none).
    QVector<Slot> lead, trail;
    if (!closed) {
        auto pairOpen = [&](int countA, int countB, int firstA, int firstB, int step) {
            // Index i counts outwards from the neighbour: firstX + step * i.
            QVector<Slot> s;
            const int n = std::min(countA, countB);
            for (int i = 0; i < std::max(countA, countB); ++i) {
                Slot slot;
                if (i < countA) slot.ia = firstA + step * i;
                else slot.sa = bare(A[firstA + step * (n - 1)]);
                if (i < countB) slot.ib = firstB + step * i;
                else slot.sb = bare(B[firstB + step * (n - 1)]);
                s.push_back(slot);
            }
            return s;
        };
        // n - 1 is -1 when a side has none: then firstX - step is the neighbour.
        lead = pairOpen(matchA.first(), matchB.first(), matchA.first() - 1, matchB.first() - 1, -1);
        std::reverse(lead.begin(), lead.end());
        trail = pairOpen(na - 1 - matchA.last(), nb - 1 - matchB.last(), matchA.last() + 1,
                         matchB.last() + 1, 1);
    }
    auto place = [&](const Slot &s) {
        outA->push_back(s.ia >= 0 ? A[s.ia] : s.sa);
        outB->push_back(s.ib >= 0 ? B[s.ib] : s.sb);
    };
    for (const Slot &s : lead) place(s);
    for (int g = 0; g < k; ++g) {
        outA->push_back(A[matchA[g]]);
        outB->push_back(B[matchB[g]]);
        for (const Slot &s : gapSlots[g]) place(s);
    }
    for (const Slot &s : trail) place(s);
}

/** Two contours of one mask in between their keyframes. */
MaskPath morphPath(const MaskPath &a, const MaskPath &b, double t)
{
    QVector<MaskAnchor> ca, cb;
    correspond(a, b, &ca, &cb);
    MaskPath r = a;
    r.anchors.clear();
    for (int i = 0; i < ca.size(); ++i) {
        r.anchors.push_back(blendAnchor(ca[i], cb[i], t));
    }
    return r;
}

} // namespace

bool maskAt(const EventPanCropState &state, double localTimeSec, MaskKeyframe *out)
{
    if (!out) {
        return false;
    }
    // A keyframe with no contours carries no shape — VEGAS writes one at the head of the
    // list whether or not a mask starts there. It is not "mask off": its own render of
    // project_big--buck-bunny_pan-crop_mask.veg shows the first real shape already in
    // place three seconds in, well before the keyframe that holds it. Treating the empty
    // one as a shape left the clip unmasked until then, which is the one frame out of six
    // that did not match its render.
    QVector<MaskKeyframe> kfs;
    kfs.reserve(state.maskKeyframes.size());
    for (const MaskKeyframe &k : state.maskKeyframes) {
        if (!k.paths.isEmpty()) {
            kfs.push_back(k);
        }
    }
    if (kfs.isEmpty()) {
        return false;
    }
    int i1 = -1;
    for (int i = 0; i < kfs.size(); ++i) {
        if (kfs[i].timeSec <= localTimeSec + 1e-9) {
            i1 = i;
        }
    }
    if (i1 < 0) {
        *out = kfs.first();
        return true;
    }
    if (i1 >= kfs.size() - 1) {
        *out = kfs.last();
        return true;
    }
    const MaskKeyframe &a = kfs[i1];
    const MaskKeyframe &b = kfs[i1 + 1];

    // A mask morphs the way every other keyframed value does: anchor by anchor, tangents
    // included. Holding the earlier keyframe until the next one arrives — which is what
    // this used to do — makes the shape jump at each keyframe instead of travelling.
    if (a.type == VideoKeyframeType::Hold) {
        *out = a;
        return true;
    }

    // Contours pair by their id, as Mask::Interpolate pairs them, and each pair morphs even
    // when anchors were added or removed in between — this sample runs 14/4/4 → 14/5/4 →
    // 13/4/4 → 14/4/3. Holding such a contour, which this used to do, left the ellipse in
    // project_big--buck-bunny_pan-crop_mask.veg standing still for twelve seconds while
    // VEGAS's own render moves it; a contour with no partner is left as it is, as there.
    const double u = videoKeyframeEase(a.type, segmentT(a, b, localTimeSec));
    MaskKeyframe r = a;
    r.timeSec = localTimeSec;
    for (int pi = 0; pi < r.paths.size(); ++pi) {
        const MaskPath &pa = a.paths[pi];
        int partner = -1;
        if (pa.id >= 0) {
            for (int pj = 0; pj < b.paths.size(); ++pj) {
                if (b.paths[pj].id == pa.id) {
                    partner = pj;
                    break;
                }
            }
        } else if (pi < b.paths.size() && b.paths[pi].id < 0) {
            partner = pi;
        }
        if (partner < 0 || pa.anchors.isEmpty() || b.paths[partner].anchors.isEmpty()) {
            continue;
        }
        r.paths[pi] = morphPath(pa, b.paths[partner], u);
    }
    *out = r;
    return true;
}

} // namespace openvegas
