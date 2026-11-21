#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../../runtime/object_model/Object.h"
#include "../android.graphics/Rect.h"
#include "../android.os/Message.h"

namespace NativeDex::Android::View {

using Graphics::Rect;

class ViewParent;
class ViewGroup;

namespace Graphics {
    class Canvas;
}
using Graphics::Canvas;

class MotionEvent;
class KeyEvent;

class View : public Runtime::ObjectModel::Object {
public:
    View();
    virtual ~View();
    
    // Layout
    virtual void measure(int widthSpec, int heightSpec);
    virtual void layout(int left, int top, int right, int bottom);
    virtual void onMeasure(int widthSpec, int heightSpec) {}
    virtual void onLayout(bool changed, int left, int top, int right, int bottom) {}
    
    // Drawing
    virtual void draw(Canvas* canvas);
    virtual void onDraw(Canvas* canvas) {}
    
    // Touch/Input
    virtual bool onTouchEvent(MotionEvent* event) { return false; }
    virtual bool onKeyDown(int keyCode, KeyEvent* event) { return false; }
    virtual bool onKeyUp(int keyCode, KeyEvent* event) { return false; }
    
    // Focus
    virtual void requestFocus() {}
    virtual bool hasFocus() { return false; }
    
    // Visibility
    enum Visibility { VISIBLE = 0, INVISIBLE = 4, GONE = 8 };
    void setVisibility(int visibility) { visibility_ = visibility; }
    int getVisibility() const { return visibility_; }
    
    // Parent
    void setParent(ViewParent* parent) { parent_ = parent; }
    ViewParent* getParent() const { return parent_; }
    
    // ID
    void setId(int id) { id_ = id; }
    int getId() const { return id_; }
    
    // Tag
    void setTag(void* tag) { tag_ = tag; }
    void* getTag() const { return tag_; }
    
    // Layout params
    struct LayoutParams {
        int width = 0;
        int height = 0;
        int marginLeft = 0;
        int marginTop = 0;
        int marginRight = 0;
        int marginBottom = 0;
        int gravity = 0;
    };
    
    void setLayoutParams(const LayoutParams& params) { layoutParams_ = params; }
    const LayoutParams& getLayoutParams() const { return layoutParams_; }
    LayoutParams& getLayoutParams() { return layoutParams_; }
    
    // Measured dimensions
    int getMeasuredWidth() const { return measuredWidth_; }
    int getMeasuredHeight() const { return measuredHeight_; }
    void setMeasuredDimension(int width, int height) {
        measuredWidth_ = width;
        measuredHeight_ = height;
    }
    
    // Actual dimensions
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getLeft() const { return left_; }
    int getTop() const { return top_; }
    int getRight() const { return right_; }
    int getBottom() const { return bottom_; }
    
    // Click listeners
    using OnClickListener = std::function<void(View*)>;
    void setOnClickListener(OnClickListener listener) { clickListener_ = listener; }
    
    // Invalidation
    void invalidate() { invalidated_ = true; }
    void invalidate(const Rect& dirty) { invalidated_ = true; dirtyRect_ = dirty; }
    
    // Background
    void setBackgroundColor(int color) { backgroundColor_ = color; }
    int getBackgroundColor() const { return backgroundColor_; }

protected:
    int id_ = -1;
    void* tag_ = nullptr;
    ViewParent* parent_ = nullptr;
    int visibility_ = VISIBLE;
    LayoutParams layoutParams_;
    int measuredWidth_ = 0;
    int measuredHeight_ = 0;
    int width_ = 0;
    int height_ = 0;
    int left_ = 0;
    int top_ = 0;
    int right_ = 0;
    int bottom_ = 0;
    int backgroundColor_ = 0;
    bool invalidated_ = false;
    Rect dirtyRect_;
    OnClickListener clickListener_;
};

} // namespace NativeDex::Android::View