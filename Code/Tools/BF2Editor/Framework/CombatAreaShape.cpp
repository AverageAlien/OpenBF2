// Code/Tools/BF2Editor/Framework/CombatAreaShape.cpp - see CombatAreaShape.h. Editor-only code.
#include "Framework/EditorConfig.h"
#include "Framework/CombatAreaShape.h"

#include <algorithm>
#include <cmath>

namespace bf2editor
{

namespace
{

typedef std::pair<float, float> Point;

// The sign of the turn a -> b -> c: 1 anticlockwise, -1 clockwise, 0 in line.
int turn(const Point& a, const Point& b, const Point& c)
{
    const double cross = (static_cast<double>(b.first) - a.first) * (static_cast<double>(c.second) - a.second)
                       - (static_cast<double>(b.second) - a.second) * (static_cast<double>(c.first) - a.first);
    return cross > 0.0 ? 1 : (cross < 0.0 ? -1 : 0);
}

// Whether p, in line with a-b, lies within the segment's box.
bool within(const Point& a, const Point& b, const Point& p)
{
    return std::min(a.first, b.first) <= p.first && p.first <= std::max(a.first, b.first)
        && std::min(a.second, b.second) <= p.second && p.second <= std::max(a.second, b.second);
}

// Whether the chain doubles back at `at`: the edges from -> at and at -> to in line, `to` heading back over the first.
bool doublesBack(const Point& from, const Point& at, const Point& to)
{
    if (turn(from, at, to) != 0)
        return false;
    const double dot = (static_cast<double>(from.first) - at.first) * (static_cast<double>(to.first) - at.first)
                     + (static_cast<double>(from.second) - at.second) * (static_cast<double>(to.second) - at.second);
    return dot > 0.0;
}

// CombatArea::checkPositionPolygon: crossing parity, horizontal edges skipped, all in float.
bool engineInside(const AreaOutline& points, float x, float z)
{
    bool inside = false;
    const size_t count = points.size();
    for (size_t i = 0; i < count; ++i)
    {
        const Point& cur = points[i];
        const Point& nxt = points[(i + 1) % count];
        const bool curAbove = z > cur.second;
        const bool nxtAbove = z > nxt.second;
        if (curAbove == nxtAbove)
            continue;
        const float crossX = (z - cur.second) / (nxt.second - cur.second) * (nxt.first - cur.first) + cur.first;
        if (x > crossX)
            inside = !inside;
    }
    return inside;
}

AreaOutline rotated(const AreaOutline& points, size_t start)
{
    AreaOutline out;
    out.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        out.push_back(points[(start + i) % points.size()]);
    return out;
}

} // namespace

double areaSignedArea(const AreaOutline& points)
{
    double sum = 0.0;
    const size_t count = points.size();
    for (size_t i = 0; i < count; ++i)
    {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % count];
        sum += static_cast<double>(a.first) * b.second - static_cast<double>(b.first) * a.second;
    }
    return sum * 0.5;
}

bool areaEngineSaysClockwise(const AreaOutline& points)
{
    if (points.size() < 2)
        return false;
    const float dx = points[1].first - points[0].first;
    const float dz = points[1].second - points[0].second;
    const float x = -dz + (points[1].first + points[0].first) * 0.5f;
    const float z = dx + (points[1].second + points[0].second) * 0.5f;
    return !engineInside(points, x, z);
}

bool areaDirectionReadable(const AreaOutline& points)
{
    return points.size() >= 3 && areaEngineSaysClockwise(points) == (areaSignedArea(points) < 0.0);
}

bool areaSegmentsTouch(const Point& a, const Point& b, const Point& c, const Point& d)
{
    const int t1 = turn(a, b, c), t2 = turn(a, b, d), t3 = turn(c, d, a), t4 = turn(c, d, b);
    if (t1 != t2 && t3 != t4)
        return true;
    return (t1 == 0 && within(a, b, c)) || (t2 == 0 && within(a, b, d)) || (t3 == 0 && within(c, d, a))
        || (t4 == 0 && within(c, d, b));
}

int areaChainCrossing(const AreaOutline& chain, const Point& next)
{
    const size_t n = chain.size();
    if (n < 2)
        return -1;
    const Point& last = chain[n - 1];
    // The edge it joins: only doubling back over it counts.
    if (doublesBack(chain[n - 2], last, next))
        return static_cast<int>(n - 2);
    for (size_t i = 0; i + 2 < n; ++i)
        if (areaSegmentsTouch(chain[i], chain[i + 1], last, next))
            return static_cast<int>(i);
    return -1;
}

bool areaClosingCrosses(const AreaOutline& chain)
{
    const size_t n = chain.size();
    if (n < 3)
        return false;
    const Point& first = chain[0];
    const Point& last = chain[n - 1];
    if (doublesBack(chain[n - 2], last, first) || doublesBack(last, first, chain[1]))
        return true;
    for (size_t i = 1; i + 2 < n; ++i)
        if (areaSegmentsTouch(chain[i], chain[i + 1], last, first))
            return true;
    return false;
}

bool areaSelfIntersects(const AreaOutline& points)
{
    const size_t n = points.size();
    if (n < 3)
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        if (doublesBack(points[(i + n - 1) % n], points[i], points[(i + 1) % n]))
            return true;
        for (size_t j = i + 2; j < n; ++j)
        {
            if (i == 0 && j == n - 1)
                continue;       // the closing edge joins the first
            if (areaSegmentsTouch(points[i], points[(i + 1) % n], points[j], points[(j + 1) % n]))
                return true;
        }
    }
    return false;
}

bool areaOrientForEngine(AreaOutline& points)
{
    if (points.size() < 3 || std::fabs(areaSignedArea(points)) < 1.0 || areaSelfIntersects(points))
        return false;
    AreaOutline work = points;
    if (areaSignedArea(work) > 0.0)
        std::reverse(work.begin(), work.end());
    for (size_t start = 0; start < work.size(); ++start)
    {
        const AreaOutline candidate = rotated(work, start);
        if (areaEngineSaysClockwise(candidate))
        {
            points = candidate;
            return true;
        }
    }
    // No start reads right: halve the shortest edge until its probe falls just outside.
    size_t shortest = 0;
    double best = -1.0;
    for (size_t i = 0; i < work.size(); ++i)
    {
        const Point& a = work[i];
        const Point& b = work[(i + 1) % work.size()];
        const double length = std::hypot(static_cast<double>(b.first) - a.first, static_cast<double>(b.second) - a.second);
        if (best < 0.0 || length < best)
        {
            best = length;
            shortest = i;
        }
    }
    AreaOutline candidate = rotated(work, shortest);
    for (int split = 0; split < 20; ++split)
    {
        const Point middle((candidate[0].first + candidate[1].first) * 0.5f,
                           (candidate[0].second + candidate[1].second) * 0.5f);
        candidate.insert(candidate.begin() + 1, middle);
        if (areaEngineSaysClockwise(candidate))
        {
            points = candidate;
            return true;
        }
    }
    return false;
}

} // namespace bf2editor
