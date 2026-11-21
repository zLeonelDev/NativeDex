#pragma once

#include <string>
#include <memory>
#include <vector>
#include "../../android/android.os/Handler.h"
#include "../../platform/windows/graphics/Direct2DGraphics.h"
#include "../../platform/windows/input/Win32Input.h"

namespace NativeDex::Launcher {

class ActivityManager;

class View {
public:
    struct LayoutParams {
        int width = -1;  // WRAP_CONTENT = -2, MATCH_PARENT = -1
        int height = -1;
        float weight = 0;
        int gravity = 0;
        int marginLeft = 0, marginTop = 0, marginRight = 0, marginBottom = 0;
    };
    
    View() = default;
    virtual ~View() = default;
    
    // Measurement
    virtual void measure(int widthSpec, int heightSpec);
    virtual void onMeasure(int widthSpec, int heightSpec);
    void setMeasuredDimension(int width, int height);
    int getMeasuredWidth() const { return measuredWidth_; }
    int getMeasuredHeight() const { return measuredHeight_; }
    
    // Layout
    virtual void layout(int left, int top, int right, int bottom);
    void setFrame(int left, int top, int right, int bottom);
    int getLeft() const { return left_; }
    int getTop() const { return top_; }
    int getRight() const { return right_; }
    int getBottom() const { return bottom_; }
    int getWidth() const { return right_ - left_; }
    int getHeight() const { return bottom_ - top_; }
    
    // Drawing
    virtual void draw(NativeDex::Platform::Windows::Direct2DGraphics& graphics);
    virtual void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& graphics);
    void invalidate();
    void requestLayout();
    
    // Touch handling
    virtual bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& event) { return false; }
    
    // Focus
    virtual bool onKeyEvent(const NativeDex::Platform::Windows::Win32Input::AndroidKeyEvent& event) { return false; }
    void requestFocus();
    bool hasFocus() const { return hasFocus_; }
    
    // Visibility
    enum Visibility { VISIBLE = 0, INVISIBLE = 4, GONE = 8 };
    void setVisibility(Visibility v) { visibility_ = v; }
    Visibility getVisibility() const { return visibility_; }
    
    // Background
    void setBackgroundColor(uint32_t color) { backgroundColor_ = color; }
    
    // ID
    void setId(int id) { id_ = id; }
    int getId() const { return id_; }
    
    // Parent
    void setParent(View* parent) { parent_ = parent; }
    View* getParent() const { return parent_; }
    
    // Layout params
    void setLayoutParams(const LayoutParams& params) { layoutParams_ = params; }
    const LayoutParams& getLayoutParams() const { return layoutParams_; }
    LayoutParams& getLayoutParams() { return layoutParams_; }
    
    // Find view by ID (default: not a ViewGroup)
    virtual View* findViewById(int id) { return (id_ == id) ? this : nullptr; }

protected:
    int left_ = 0, top_ = 0, right_ = 0, bottom_ = 0;
    int measuredWidth_ = 0, measuredHeight_ = 0;
    Visibility visibility_ = VISIBLE;
    uint32_t backgroundColor_ = 0;
    int id_ = -1;
    View* parent_ = nullptr;
    LayoutParams layoutParams_;
    bool hasFocus_ = false;
    bool layoutRequested_ = false;
    bool measured_ = false;
};

class ViewGroup : public View {
public:
    ViewGroup() = default;
    ~ViewGroup() override = default;
    
    void addView(std::unique_ptr<View> view);
    void removeView(View* view);
    void removeAllViews();
    const std::vector<std::unique_ptr<View>>& getChildren() const { return children_; }
    View* findViewById(int id);
    
    virtual void onMeasure(int widthSpec, int heightSpec) override;
    void layout(int left, int top, int right, int bottom) override {
        View::layout(left, top, right, bottom);
        onLayout(true, left, top, right, bottom);
    }
    virtual void onLayout(bool changed, int left, int top, int right, int bottom) { (void)changed; (void)left; (void)top; (void)right; (void)bottom; }
    virtual void onDraw(NativeDex::Platform::Windows::Direct2DGraphics& graphics) override;
    virtual bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& event) override;
    
protected:
    std::vector<std::unique_ptr<View>> children_;
    virtual void dispatchDraw(NativeDex::Platform::Windows::Direct2DGraphics& graphics);
};

class Activity {
public:
    Activity(ActivityManager& manager, uint32_t classIdx, const std::string& descriptor);
    ~Activity();
    
    // Lifecycle
    void onCreate();
    void onStart();
    void onResume();
    void onPause();
    void onStop();
    void onDestroy();
    void onRestart();
    void onConfigurationChanged();
    void onLowMemory();
    
    // View hierarchy
    void setContentView(std::unique_ptr<View> view);
    void setContentView(int layoutResId); // From XML
    View* getContentView() const { return contentView_.get(); }
    
    // Find view by ID
    View* findViewById(int id);
    
    // Window
    void setTitle(const std::string& title);
    
    // Input
    bool onKeyEvent(const NativeDex::Platform::Windows::Win32Input::AndroidKeyEvent& event);
    bool onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& event);
    
    // Draw
    void draw(NativeDex::Platform::Windows::Direct2DGraphics& graphics);
    
    // Accessors
    uint32_t getClassIndex() const { return classIdx_; }
    const std::string& getDescriptor() const { return descriptor_; }
    ActivityManager& getManager() const { return manager_; }

private:
    ActivityManager& manager_;
    uint32_t classIdx_;
    std::string descriptor_;
    std::unique_ptr<View> contentView_;
    std::string title_;
};

} // namespace NativeDex::Launcher