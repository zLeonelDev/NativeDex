#include "View.h"
#include "../android.graphics/Canvas.h"

namespace NativeDex::Android::View {

View::View() = default;

View::~View() = default;

void View::measure(int widthSpec, int heightSpec) {
    onMeasure(widthSpec, heightSpec);
}

void View::layout(int left, int top, int right, int bottom) {
    left_ = left;
    top_ = top;
    right_ = right;
    bottom_ = bottom;
    width_ = right - left;
    height_ = bottom - top;
    onLayout(true, left, top, right, bottom);
}

void View::draw(Canvas* canvas) {
    if (visibility_ != VISIBLE) return;
    
    // Draw background
    if (backgroundColor_ != 0) {
        // Would draw background rect
    }
    
    // Draw self
    onDraw(canvas);
}

} // namespace NativeDex::Android::View