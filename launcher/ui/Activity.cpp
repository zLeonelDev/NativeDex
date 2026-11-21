#include "Activity.h"
#include "ActivityManager.h"
// Undefine Windows min/max macros
#undef min
#undef max
#include <iostream>
#include <algorithm>

namespace NativeDex::Launcher {

// View implementation
void View::measure(int widthSpec, int heightSpec) {
    if (measured_ && !layoutRequested_) return;
    
    int widthMode = widthSpec & 0x3;
    int widthSize = widthSpec >> 2;
    int heightMode = heightSpec & 0x3;
    int heightSize = heightSpec >> 2;
    
    onMeasure(widthSpec, heightSpec);
    measured_ = true;
    layoutRequested_ = false;
}

void View::onMeasure(int widthSpec, int heightSpec) {
    int widthMode = widthSpec & 0x3;
    int widthSize = widthSpec >> 2;
    int heightMode = heightSpec & 0x3;
    int heightSize = heightSpec >> 2;
    
    int width = 0, height = 0;
    
    if (layoutParams_.width == -1) { // MATCH_PARENT
        width = widthSize;
    } else if (layoutParams_.width == -2) { // WRAP_CONTENT
        width = 100; // Default
    } else {
        width = layoutParams_.width;
    }
    
    if (layoutParams_.height == -1) {
        height = heightSize;
    } else if (layoutParams_.height == -2) {
        height = 50;
    } else {
        height = layoutParams_.height;
    }
    
    setMeasuredDimension(width, height);
}

void View::setMeasuredDimension(int width, int height) {
    measuredWidth_ = width;
    measuredHeight_ = height;
}

void View::layout(int left, int top, int right, int bottom) {
    setFrame(left, top, right, bottom);
}

void View::setFrame(int left, int top, int right, int bottom) {
    left_ = left;
    top_ = top;
    right_ = right;
    bottom_ = bottom;
}

void View::draw(NativeDex::Platform::Windows::Direct2DGraphics& graphics) {
    if (visibility_ != VISIBLE) return;
    
    // Draw background
    if (backgroundColor_ != 0) {
        NativeDex::Platform::Windows::Direct2DGraphics::Rect rect(left_, top_, right_, bottom_);
        NativeDex::Platform::Windows::Direct2DGraphics::Color color = NativeDex::Platform::Windows::Direct2DGraphics::Color::fromArgb(backgroundColor_);
        graphics.fillRect(rect, color);
    }
    
    onDraw(graphics);
}

void View::onDraw(NativeDex::Platform::Windows::Direct2DGraphics& graphics) {
    // Override in subclasses
}

void View::invalidate() {
    // Would trigger redraw
}

void View::requestLayout() {
    layoutRequested_ = true;
    measured_ = false;
    if (parent_) parent_->requestLayout();
}

void View::requestFocus() {
    hasFocus_ = true;
}

// ViewGroup implementation
void ViewGroup::addView(std::unique_ptr<View> view) {
    if (view) {
        view->setParent(this);
        children_.push_back(std::move(view));
    }
}

void ViewGroup::removeView(View* view) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == view) {
            children_.erase(it);
            break;
        }
    }
}

void ViewGroup::removeAllViews() {
    children_.clear();
}

View* ViewGroup::findViewById(int id) {
    if (id_ == id) return this;
    for (auto& child : children_) {
        View* found = child->findViewById(id);
        if (found) return found;
    }
    return nullptr;
}

void ViewGroup::onMeasure(int widthSpec, int heightSpec) {
    // Default: measure all children
    for (auto& child : children_) {
        int childWidthSpec = widthSpec;
        int childHeightSpec = heightSpec;
        
        const auto& params = child->getLayoutParams();
        if (params.width != -1 && params.width != -2) {
            childWidthSpec = (params.width << 2) | 0; // EXACTLY
        }
        if (params.height != -1 && params.height != -2) {
            childHeightSpec = (params.height << 2) | 0;
        }
        
        child->measure(childWidthSpec, childHeightSpec);
    }
    
    // Use largest child size
    int maxWidth = 0, maxHeight = 0;
    for (auto& child : children_) {
        if (child->getMeasuredWidth() > maxWidth) maxWidth = child->getMeasuredWidth();
        if (child->getMeasuredHeight() > maxHeight) maxHeight = child->getMeasuredHeight();
    }
    
    setMeasuredDimension(maxWidth, maxHeight);
}

void ViewGroup::onDraw(NativeDex::Platform::Windows::Direct2DGraphics& graphics) {
    dispatchDraw(graphics);
}

bool ViewGroup::onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& event) {
    // Dispatch to children in reverse order (topmost first)
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->onTouchEvent(event)) {
            return true;
        }
    }
    return false;
}

void ViewGroup::dispatchDraw(NativeDex::Platform::Windows::Direct2DGraphics& graphics) {
    for (auto& child : children_) {
        if (child->getVisibility() == VISIBLE) {
            graphics.pushTransform();
            graphics.translate(static_cast<float>(child->getLeft()), static_cast<float>(child->getTop()));
            child->draw(graphics);
            graphics.popTransform();
        }
    }
}

// Activity implementation
Activity::Activity(ActivityManager& manager, uint32_t classIdx, const std::string& descriptor)
    : manager_(manager), classIdx_(classIdx), descriptor_(descriptor) {}

Activity::~Activity() {}

void Activity::onCreate() {
    // Override in subclass
}

void Activity::onStart() {}
void Activity::onResume() {}
void Activity::onPause() {}
void Activity::onStop() {}
void Activity::onDestroy() {}
void Activity::onRestart() {}
void Activity::onConfigurationChanged() {}
void Activity::onLowMemory() {}

void Activity::setContentView(std::unique_ptr<View> view) {
    contentView_ = std::move(view);
}

void Activity::setContentView(int layoutResId) {
    // Would inflate from XML resource
}

View* Activity::findViewById(int id) {
    if (contentView_) {
        return contentView_->findViewById(id);
    }
    return nullptr;
}

void Activity::setTitle(const std::string& title) {
    title_ = title;
    // Would update window title
}

bool Activity::onKeyEvent(const NativeDex::Platform::Windows::Win32Input::AndroidKeyEvent& event) {
    if (contentView_) {
        return contentView_->onKeyEvent(event);
    }
    return false;
}

bool Activity::onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& event) {
    if (contentView_) {
        return contentView_->onTouchEvent(event);
    }
    return false;
}

void Activity::draw(NativeDex::Platform::Windows::Direct2DGraphics& graphics) {
    if (contentView_ && contentView_->getVisibility() == View::VISIBLE) {
        contentView_->draw(graphics);
    }
}

} // namespace NativeDex::Launcher
