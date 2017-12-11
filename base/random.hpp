#pragma once

namespace base
{

float random_unit_float()
{
    return std::rand() / static_cast<float>(RAND_MAX);
}

float random_range(float low, float high)
{
    return low + (high - low)*random_unit_float();
}
} // namespace base