#pragma once

#include <algorithm>

// Fixed-timestep accumulator. Simulation runs at a constant rate; rendering
// interpolates between the last two simulation states using alpha().
struct FixedStep {
    double dt = 1.0 / 240.0;
    double accumulator = 0.0;
    int maxStepsPerFrame = 24; // avoid the spiral of death after a hitch

    // Returns the number of simulation steps to run this frame.
    int advance(double frameTime) {
        accumulator += std::min(frameTime, 0.25);
        int steps = 0;
        while (accumulator >= dt && steps < maxStepsPerFrame) {
            accumulator -= dt;
            ++steps;
        }
        if (steps == maxStepsPerFrame) accumulator = 0.0;
        return steps;
    }

    float alpha() const { return float(accumulator / dt); }
};
