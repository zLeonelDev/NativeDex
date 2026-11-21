#pragma once

namespace NativeDex::Android::Graphics {

struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    
    Rect() = default;
    Rect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}
};

} // namespace NativeDex::Android::Graphics
