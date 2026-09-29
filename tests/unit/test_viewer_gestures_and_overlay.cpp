#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "test_helpers.h"
#include "Viewer.h"
#include "ImageList.h"
#include "ImageListFilter.h"
#include "QuiverFile.h"
#include "QuiverUtils.h"
#include "QuiverRotatedPaintable.h"
#include "quiver-image-view.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include <string>
#include <vector>
#include <fstream>
#include <cmath>
#include <dlfcn.h>

/* Move the X pointer, so a zoom that anchors to the pointer can be exercised:
 * GTK4 has no way to ask for one to be placed, and without a pointer under the
 * widget every zoom is about the middle by construction.  XTEST is loaded
 * rather than linked so that a build without it still compiles - the tests that
 * need it say so rather than passing vacuously. */
static bool quiver_test_warp_pointer(int root_x, int root_y)
{
    static void *xtst = dlopen("libXtst.so.6", RTLD_LAZY);
    if (xtst == NULL)
        xtst = dlopen("libXtst.so", RTLD_LAZY);
    if (xtst == NULL)
        return false;
    typedef int (*XTestFakeMotionEventFn)(void *, int, int, int, unsigned long);
    XTestFakeMotionEventFn move = (XTestFakeMotionEventFn)dlsym(xtst, "XTestFakeMotionEvent");
    if (move == NULL)
        return false;
    return move(xtst, -1 /* DefaultScreen */, root_x, root_y, 0) == 1;
}

/* The root-window position of a point given as a fraction of a widget, which is
 * how a test says "the pointer is a quarter of the way in from the left". */
static bool quiver_test_pointer_over(GtkWidget *widget, double fx, double fy, int *root_x,
                                     int *root_y)
{
    if (widget == NULL || !gtk_widget_get_realized(widget))
        return false;
    const int w = gtk_widget_get_width(widget);
    const int h = gtk_widget_get_height(widget);
    if (w <= 0 || h <= 0)
        return false;
    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(widget));
    if (root == NULL || !gtk_widget_get_realized(root))
        return false;
    graphene_point_t origin = GRAPHENE_POINT_INIT(0.f, 0.f);
    graphene_point_t at;
    if (!gtk_widget_compute_point(widget, root, &origin, &at))
        return false;
    *root_x = (int)(at.x + fx * w);
    *root_y = (int)(at.y + fy * h);
    return quiver_test_warp_pointer(*root_x, *root_y);
}

TEST_CASE("Viewer Control Overlays Structure and Styling", "[unit][viewer][overlay]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);
    REQUIRE(GTK_IS_BOX(bar));
    REQUIRE(gtk_widget_has_css_class(bar, "viewer-overlay-bar"));

    GtkWidget *overlay = viewer->GetOverlay();
    REQUIRE(overlay != nullptr);
    REQUIRE(GTK_IS_OVERLAY(overlay));
    REQUIRE(gtk_overlay_get_measure_overlay(GTK_OVERLAY(overlay), bar) == FALSE);

    GtkWidget *timeline = viewer->GetTimelineRow();
    REQUIRE(timeline != nullptr);
    REQUIRE(GTK_IS_BOX(timeline));
    REQUIRE(gtk_widget_has_css_class(timeline, "timeline-overlay-bar"));
    REQUIRE(gtk_overlay_get_measure_overlay(GTK_OVERLAY(overlay), timeline) == FALSE);

    // Verify overlay buttons and their classes across the unified HUD
    std::vector<std::string> tooltips;
    std::vector<GtkWidget*> buttons;
    int button_count = 0;
    int separator_count = 0;

    auto collect_children = [&](auto &self, GtkWidget *parent) -> void {
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
        {
            if (GTK_IS_BUTTON(child) || GTK_IS_MENU_BUTTON(child))
            {
                /* the view-mode arrow is the one button in the bar that is
                 * deliberately not a .media-btn - that class makes every HUD
                 * button a square 2.4em box, far too wide for a bare arrow -
                 * so it is not one of the media buttons this walks */
                if (gtk_widget_has_css_class(child, "view-mode-arrow"))
                {
                    self(self, child);
                    continue;
                }
                button_count++;
                buttons.push_back(child);
                REQUIRE(gtk_widget_has_css_class(child, "media-btn"));
                const char *tip = gtk_widget_get_tooltip_text(child);
                if (tip)
                {
                    tooltips.push_back(tip);
                }
            }
            else if (GTK_IS_SEPARATOR(child))
            {
                separator_count++;
            }
            if (GTK_IS_BOX(child))
            {
                self(self, child);
            }
        }
    };
    collect_children(collect_children, bar);

    // Unified HUD contains 11 slots for both image and video
    REQUIRE(button_count >= 11);
    REQUIRE(tooltips.size() >= 8);

    // Check specific tooltips
    bool has_prev = false, has_next = false;
    bool has_zoom = false, has_rotate = false, has_fs = false;
    bool has_rewind = false, has_ff = false;
    bool has_options = false;

    for (const auto &t : tooltips)
    {
        if (t.find("Previous") != std::string::npos) has_prev = true;
        if (t.find("Next") != std::string::npos) has_next = true;
        if (t.find("Zoom") != std::string::npos) has_zoom = true;
        if (t.find("Rotate") != std::string::npos) has_rotate = true;
        if (t.find("Fullscreen") != std::string::npos) has_fs = true;
        if (t.find("Backwards") != std::string::npos || t.find("Rewind") != std::string::npos) has_rewind = true;
        if (t.find("Forward") != std::string::npos) has_ff = true;
        if (t.find("Options") != std::string::npos) has_options = true;
    }

    REQUIRE(has_prev);
    REQUIRE(has_next);
    REQUIRE(has_zoom);
    REQUIRE(has_rotate);
    REQUIRE(has_fs);
    REQUIRE(has_rewind);
    REQUIRE(has_ff);
    REQUIRE(has_options);

    GtkWidget *centerPlay = viewer->GetCenterPlayButton();
    REQUIRE(centerPlay != nullptr);
    REQUIRE(GTK_IS_BUTTON(centerPlay));
    REQUIRE(gtk_widget_has_css_class(centerPlay, "center-play-btn"));
    REQUIRE(gtk_widget_has_css_class(centerPlay, "circular"));
}

TEST_CASE("Viewer Slideshow State Management", "[unit][viewer][slideshow]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    REQUIRE_FALSE(viewer->IsSlideShowRunning());
    REQUIRE_FALSE(viewer->IsSlideShowPaused());

    // Pausing or resuming when not running has no effect
    viewer->SlideShowPause();
    REQUIRE_FALSE(viewer->IsSlideShowRunning());
    REQUIRE_FALSE(viewer->IsSlideShowPaused());

    viewer->SlideShowResume();
    REQUIRE_FALSE(viewer->IsSlideShowRunning());
    REQUIRE_FALSE(viewer->IsSlideShowPaused());

    // Setup an ImageList with at least 2 real items
    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_4k.jpg");
    files.push_back(imgDir + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    // Start slideshow
    viewer->SlideShowStart();
    REQUIRE(viewer->IsSlideShowRunning());
    REQUIRE_FALSE(viewer->IsSlideShowPaused());

    // Pause slideshow
    viewer->SlideShowPause();
    REQUIRE(viewer->IsSlideShowRunning());
    REQUIRE(viewer->IsSlideShowPaused());

    // Redundant pause does not change state
    viewer->SlideShowPause();
    REQUIRE(viewer->IsSlideShowRunning());
    REQUIRE(viewer->IsSlideShowPaused());

    // Resume slideshow
    viewer->SlideShowResume();
    REQUIRE(viewer->IsSlideShowRunning());
    REQUIRE_FALSE(viewer->IsSlideShowPaused());

    // Toggle pause (should pause)
    viewer->SlideShowTogglePause();
    REQUIRE(viewer->IsSlideShowRunning());
    REQUIRE(viewer->IsSlideShowPaused());

    // Toggle pause (should resume)
    viewer->SlideShowTogglePause();
    REQUIRE(viewer->IsSlideShowRunning());
    REQUIRE_FALSE(viewer->IsSlideShowPaused());

    // Stop slideshow
    viewer->SlideShowStop();
    REQUIRE_FALSE(viewer->IsSlideShowRunning());
    REQUIRE_FALSE(viewer->IsSlideShowPaused());
}

TEST_CASE("Viewer GTK4 Gesture Controllers", "[unit][viewer][gestures]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    GtkWidget *overlay = viewer->GetOverlay();
    REQUIRE(overlay != nullptr);

    GtkWidget *stack = gtk_overlay_get_child(GTK_OVERLAY(overlay));
    REQUIRE(stack != nullptr);
    REQUIRE(GTK_IS_STACK(stack));

    GtkWidget *imageView = gtk_stack_get_child_by_name(GTK_STACK(stack), "image");
    REQUIRE(imageView != nullptr);

    GListModel *controllers = gtk_widget_observe_controllers(imageView);
    REQUIRE(controllers != nullptr);

    guint n_items = g_list_model_get_n_items(controllers);
    REQUIRE(n_items > 0);

    GtkGestureZoom *zoomGesture = nullptr;
    GtkGestureDrag *twoFingerPanGesture = nullptr;
    GtkGestureSwipe *swipeGesture = nullptr;
    GtkGestureClick *clickGesture = nullptr;
    GtkEventControllerScroll *scrollController = nullptr;

    for (guint i = 0; i < n_items; i++)
    {
        GObject *obj = G_OBJECT(g_list_model_get_item(controllers, i));
        if (GTK_IS_GESTURE_ZOOM(obj))
        {
            zoomGesture = GTK_GESTURE_ZOOM(obj);
        }
        else if (GTK_IS_GESTURE_DRAG(obj))
        {
            guint n_points = 0;
            g_object_get(obj, "n-points", &n_points, NULL);
            if (n_points == 2)
            {
                twoFingerPanGesture = GTK_GESTURE_DRAG(obj);
            }
        }
        else if (GTK_IS_GESTURE_SWIPE(obj))
        {
            swipeGesture = GTK_GESTURE_SWIPE(obj);
        }
        else if (GTK_IS_GESTURE_CLICK(obj))
        {
            guint btn = 1;
            g_object_get(obj, "button", &btn, NULL);
            if (btn == 0)
            {
                clickGesture = GTK_GESTURE_CLICK(obj);
            }
        }
        else if (GTK_IS_EVENT_CONTROLLER_SCROLL(obj))
        {
            scrollController = GTK_EVENT_CONTROLLER_SCROLL(obj);
        }
        g_object_unref(obj);
    }
    g_object_unref(controllers);

    REQUIRE(zoomGesture != nullptr);
    REQUIRE(twoFingerPanGesture != nullptr);
    REQUIRE(swipeGesture != nullptr);
    REQUIRE(clickGesture != nullptr);
    REQUIRE(scrollController != nullptr);

    // Verify swipe gesture is touch-only to ignore mouse and touchpad click-drags
    REQUIRE(gtk_gesture_single_get_touch_only(GTK_GESTURE_SINGLE(swipeGesture)) == TRUE);

    // Verify zoom and two-finger pan are grouped
    REQUIRE(gtk_gesture_is_grouped_with(GTK_GESTURE(zoomGesture), GTK_GESTURE(twoFingerPanGesture)));

    // Verify click gesture responds to button 2 (middle click)
    guint click_button = 1;
    g_object_get(clickGesture, "button", &click_button, NULL);
    REQUIRE(click_button == 0); // 0 means any button, allowing button 2 (middle click)
}

TEST_CASE("Viewer Overlay Button Sensitivities and Hover Controller", "[unit][viewer][sensitivity]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);

    // Verify motion controller on bar
    GListModel *controllers = gtk_widget_observe_controllers(bar);
    REQUIRE(controllers != nullptr);
    bool has_motion = false;
    for (guint i = 0; i < g_list_model_get_n_items(controllers); i++)
    {
        GObject *obj = (GObject*)g_list_model_get_item(controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_MOTION(obj))
        {
            has_motion = true;
        }
        g_object_unref(obj);
    }
    g_object_unref(controllers);
    REQUIRE(has_motion);

    // Find buttons recursively
    std::vector<GtkWidget*> buttons;
    auto collect_btns = [&](auto &self, GtkWidget *parent) -> void {
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
        {
            if (GTK_IS_BUTTON(child) || GTK_IS_MENU_BUTTON(child))
            {
                buttons.push_back(child);
            }
            if (GTK_IS_BOX(child))
            {
                self(self, child);
            }
        }
    };
    collect_btns(collect_btns, bar);

    auto find_btn_by_tooltip = [&](const std::string &sub) -> GtkWidget* {
        for (auto *b : buttons)
        {
            const char *t = gtk_widget_get_tooltip_text(b);
            if (t && std::string(t).find(sub) != std::string::npos)
            {
                return b;
            }
        }
        return nullptr;
    };

    GtkWidget *prevBtn = find_btn_by_tooltip("Previous");
    GtkWidget *nextBtn = find_btn_by_tooltip("Next");
    GtkWidget *zoomOutBtn = find_btn_by_tooltip("Zoom Out");
    GtkWidget *zoomFitBtn = find_btn_by_tooltip("Fit");
    GtkWidget *zoomInBtn = find_btn_by_tooltip("Zoom In");
    GtkWidget *rotCcwBtn = find_btn_by_tooltip("Counter-Clockwise");
    GtkWidget *rotCwBtn = find_btn_by_tooltip("Clockwise");
    GtkWidget *optionsBtn = find_btn_by_tooltip("Options");
    GtkWidget *fsBtn = find_btn_by_tooltip("Fullscreen");
    GtkWidget *centerPlay = viewer->GetCenterPlayButton();

    REQUIRE(prevBtn != nullptr);
    REQUIRE(nextBtn != nullptr);
    REQUIRE(zoomOutBtn != nullptr);
    REQUIRE(zoomFitBtn != nullptr);
    REQUIRE(zoomInBtn != nullptr);
    REQUIRE(rotCcwBtn != nullptr);
    REQUIRE(rotCwBtn != nullptr);
    REQUIRE(optionsBtn != nullptr);
    REQUIRE(fsBtn != nullptr);
    REQUIRE(centerPlay != nullptr);

    // Open options popup to access Slideshow
    gtk_menu_button_popup(GTK_MENU_BUTTON(optionsBtn));
    GtkPopover *popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(optionsBtn));
    REQUIRE(popover != nullptr);
    GtkWidget *slideshowBtn = nullptr;
    auto find_slideshow_btn = [&](auto &self, GtkWidget *parent) -> void {
        if (!parent) return;
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
        {
            if (GTK_IS_BUTTON(child))
            {
                const char *icon = gtk_button_get_icon_name(GTK_BUTTON(child));
                if (icon && (std::string(icon) == "display-projector-symbolic" || std::string(icon) == "media-playback-stop-symbolic"))
                {
                    slideshowBtn = child;
                    return;
                }
            }
            self(self, child);
        }
    };
    find_slideshow_btn(find_slideshow_btn, GTK_WIDGET(popover));
    REQUIRE(slideshowBtn != nullptr);

    // Initial state: empty list
    // Prev, Next, Slideshow, Rotate, Options should be insensitive
    REQUIRE_FALSE(gtk_widget_get_sensitive(prevBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(nextBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(slideshowBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(rotCcwBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(rotCwBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(optionsBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(zoomOutBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(zoomFitBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(zoomInBtn));
    // Fullscreen is always sensitive
    REQUIRE(gtk_widget_get_sensitive(fsBtn));

    // Now attach an ImageList with 2 items
    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_4k.jpg");
    files.push_back(imgDir + "/sample_video.mp4");
    list->Add(&files);

    viewer->SetImageList(list);

    // At index 0 of 2 (image):
    // Prev should be insensitive, Next should be sensitive, Slideshow sensitive, Rotate sensitive
    REQUIRE_FALSE(gtk_widget_get_sensitive(prevBtn));
    REQUIRE(gtk_widget_get_sensitive(nextBtn));
    REQUIRE(gtk_widget_get_sensitive(slideshowBtn));
    REQUIRE(gtk_widget_get_sensitive(rotCcwBtn));
    REQUIRE(gtk_widget_get_sensitive(rotCwBtn));
    REQUIRE(gtk_widget_get_sensitive(optionsBtn));
    REQUIRE_FALSE(gtk_widget_get_visible(centerPlay));

    // Advance to index 1 of 2 (video):
    list->SetCurrentIndex(1);
    // Prev should be sensitive, Next should be insensitive, center play button visible
    REQUIRE(gtk_widget_get_sensitive(prevBtn));
    REQUIRE_FALSE(gtk_widget_get_sensitive(nextBtn));
    REQUIRE(gtk_widget_get_visible(centerPlay));

    viewer->StopVideo(false);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
}

TEST_CASE("Viewer Mute and Volume Control", "[unit][viewer][mute]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    REQUIRE_FALSE(viewer->IsMuted());

    // Toggle mute on
    viewer->ToggleMute();
    REQUIRE(viewer->IsMuted());

    // Toggle mute off
    viewer->ToggleMute();
    REQUIRE_FALSE(viewer->IsMuted());

    // Set muted explicitly
    viewer->SetMuted(true);
    REQUIRE(viewer->IsMuted());

    viewer->SetMuted(false);
    REQUIRE_FALSE(viewer->IsMuted());
}

TEST_CASE("Viewer Overlay Slideshow Button and Play State", "[unit][viewer][overlay_slideshow]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);

    GtkWidget *optionsBtn = nullptr;
    for (GtkWidget *child = gtk_widget_get_first_child(bar); child != nullptr; child = gtk_widget_get_next_sibling(child))
    {
        if (GTK_IS_MENU_BUTTON(child))
        {
            const char *tip = gtk_widget_get_tooltip_text(child);
            if (tip && std::string(tip).find("Options") != std::string::npos)
            {
                optionsBtn = child;
                break;
            }
        }
    }
    REQUIRE(optionsBtn != nullptr);

    auto get_slideshow_btn = [&]() -> GtkWidget* {
        gtk_menu_button_popup(GTK_MENU_BUTTON(optionsBtn));
        GtkPopover *popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(optionsBtn));
        if (!popover) return nullptr;
        GtkWidget *btn = nullptr;
        auto find_btn = [&](auto &self, GtkWidget *parent) -> void {
            if (!parent) return;
            for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
            {
                if (GTK_IS_BUTTON(child))
                {
                    const char *icon = gtk_button_get_icon_name(GTK_BUTTON(child));
                    if (icon && (std::string(icon) == "display-projector-symbolic" || std::string(icon) == "media-playback-stop-symbolic"))
                    {
                        btn = child;
                        return;
                    }
                }
                self(self, child);
            }
        };
        find_btn(find_btn, GTK_WIDGET(popover));
        return btn;
    };

    GtkWidget *slideshowBtn = get_slideshow_btn();
    REQUIRE(slideshowBtn != nullptr);
    REQUIRE(GTK_IS_TOGGLE_BUTTON(slideshowBtn));

    // Initial state: not running
    REQUIRE_FALSE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(slideshowBtn)));
    REQUIRE(std::string(gtk_button_get_icon_name(GTK_BUTTON(slideshowBtn))) == "display-projector-symbolic");
    const char *tip = gtk_widget_get_tooltip_text(slideshowBtn);
    REQUIRE(tip != nullptr);
    REQUIRE(std::string(tip).find("(S)") != std::string::npos);

    // Setup an ImageList with at least 2 real items so slideshow can run
    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_4k.jpg");
    files.push_back(imgDir + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    // Start slideshow
    viewer->SlideShowStart();
    REQUIRE(viewer->IsSlideShowRunning());

    slideshowBtn = get_slideshow_btn();
    REQUIRE(slideshowBtn != nullptr);
    REQUIRE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(slideshowBtn)));
    REQUIRE(std::string(gtk_button_get_icon_name(GTK_BUTTON(slideshowBtn))) == "display-projector-symbolic");
    REQUIRE(gtk_widget_has_css_class(slideshowBtn, "speed-active"));

    // Clicking the submenu toggle button stops the slideshow
    g_signal_emit_by_name(slideshowBtn, "clicked");
    REQUIRE_FALSE(viewer->IsSlideShowRunning());

    slideshowBtn = get_slideshow_btn();
    REQUIRE(slideshowBtn != nullptr);
    REQUIRE_FALSE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(slideshowBtn)));
    REQUIRE(std::string(gtk_button_get_icon_name(GTK_BUTTON(slideshowBtn))) == "display-projector-symbolic");
    REQUIRE_FALSE(gtk_widget_has_css_class(slideshowBtn, "speed-active"));

    viewer->StopVideo(false);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
}

TEST_CASE("Viewer Zoom In and Zoom Out Anchor Behavior", "[unit][viewer][zoom]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();
    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);

    GtkWidget *imageview_widget = viewer->GetImageView();
    REQUIRE(imageview_widget != nullptr);
    QuiverImageView *iv = QUIVER_IMAGE_VIEW(imageview_widget);

    // Find HUD Zoom In and Zoom Out buttons
    GtkWidget *zoomInBtn = nullptr;
    GtkWidget *zoomOutBtn = nullptr;
    auto find_zoom_btns = [&](auto &self, GtkWidget *parent) -> void {
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
        {
            if (GTK_IS_BUTTON(child))
            {
                const char *tip = gtk_widget_get_tooltip_text(child);
                if (tip)
                {
                    if (std::string(tip).find("Zoom In") != std::string::npos)
                        zoomInBtn = child;
                    else if (std::string(tip).find("Zoom Out") != std::string::npos)
                        zoomOutBtn = child;
                }
            }
            if (GTK_IS_BOX(child))
            {
                self(self, child);
            }
        }
    };
    find_zoom_btns(find_zoom_btns, bar);
    REQUIRE(zoomInBtn != nullptr);
    REQUIRE(zoomOutBtn != nullptr);

    // Initial state: not set
    REQUIRE_FALSE(quiver_image_view_get_zoom_anchor_center(iv));
    REQUIRE_FALSE(viewer->IsVideoZoomAnchorCenter());

    // Setup an ImageList with sample_4k.jpg so zoom actions are enabled
    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_4k.jpg");
    list->Add(&files);
    viewer->SetImageList(list);

    // 1. Direct action activation (keyboard shortcuts +, -, =) should NOT set zoom_anchor_center
    GAction *actIn = QuiverUtils::GetAction("ZoomIn");
    REQUIRE(actIn != nullptr);
    g_action_activate(actIn, NULL);
    REQUIRE_FALSE(quiver_image_view_get_zoom_anchor_center(iv));
    REQUIRE_FALSE(viewer->IsVideoZoomAnchorCenter());

    GAction *actOut = QuiverUtils::GetAction("ZoomOut");
    REQUIRE(actOut != nullptr);
    g_action_activate(actOut, NULL);
    REQUIRE_FALSE(quiver_image_view_get_zoom_anchor_center(iv));
    REQUIRE_FALSE(viewer->IsVideoZoomAnchorCenter());

    // 2. Clicking HUD zoom in and zoom out buttons initiates centered zoom and resets upon completion
    g_signal_emit_by_name(zoomInBtn, "clicked");
    REQUIRE(quiver_image_view_get_zoom_anchor_center(iv));

    for (int i = 0; i < 50 && quiver_image_view_get_zoom_anchor_center(iv); ++i)
    {
        g_usleep(10000);
        while (g_main_context_iteration(NULL, FALSE));
    }
    REQUIRE_FALSE(quiver_image_view_get_zoom_anchor_center(iv));
    REQUIRE_FALSE(viewer->IsVideoZoomAnchorCenter());

    g_signal_emit_by_name(zoomOutBtn, "clicked");
    REQUIRE(quiver_image_view_get_zoom_anchor_center(iv));

    for (int i = 0; i < 50 && quiver_image_view_get_zoom_anchor_center(iv); ++i)
    {
        g_usleep(10000);
        while (g_main_context_iteration(NULL, FALSE));
    }
    REQUIRE_FALSE(quiver_image_view_get_zoom_anchor_center(iv));
    REQUIRE_FALSE(viewer->IsVideoZoomAnchorCenter());
}

TEST_CASE("Viewer HUD Position and Filmstrip Collision Avoidance", "[unit][viewer][hud_position]")
{
    REQUIRE_DISPLAY();

    PreferencesPtr prefs = Preferences::GetInstance();
    // Save original settings
    int origHudPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_BOTTOM);
    int origFPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_RIGHT);
    bool origOverlay = prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);
    bool origShow = prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, true);

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();
    GtkWidget *bar = viewer->GetViewerOverlayBar();
    GtkWidget *timeline = viewer->GetTimelineRow();
    REQUIRE(bar != nullptr);
    REQUIRE(timeline != nullptr);

    // Default: Bottom, no bottom filmstrip collision -> margin_bottom is 50 for bar, 12 for timeline
    prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_BOTTOM);
    prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_LEFT);
    viewer->UpdateHUDPosition();
    REQUIRE(gtk_widget_get_valign(bar) == GTK_ALIGN_END);
    REQUIRE(gtk_widget_get_margin_bottom(bar) == 50);
    REQUIRE(gtk_widget_get_margin_bottom(timeline) == 12);

    // Filmstrip at bottom with overlay enabled -> HUD bar and timeline offset upwards to avoid collision
    prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_BOTTOM);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, true);
    QuiverUtils::ToggleActionSetActive("ViewFilmStrip", TRUE);
    viewer->UpdateHUDPosition();
    REQUIRE(gtk_widget_get_valign(bar) == GTK_ALIGN_END);
    REQUIRE(gtk_widget_get_margin_bottom(bar) > 50);
    REQUIRE(gtk_widget_get_margin_bottom(timeline) > 12);

    // HUD position at top -> valign is GTK_ALIGN_START
    prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_TOP);
    viewer->UpdateHUDPosition();
    REQUIRE(gtk_widget_get_valign(bar) == GTK_ALIGN_START);
    REQUIRE(gtk_widget_get_margin_top(bar) >= 50);
    REQUIRE(gtk_widget_get_valign(timeline) == GTK_ALIGN_START);
    REQUIRE(gtk_widget_get_margin_top(timeline) >= 12);

    // Restore original settings
    prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, origHudPos);
    prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, origFPos);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, origOverlay);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, origShow);
    QuiverUtils::ToggleActionSetActive("ViewFilmStrip", origShow);
}

TEST_CASE("Viewer HUD and Filmstrip Auto-Hide Timeout", "[unit][viewer][timeout]")
{
    REQUIRE_DISPLAY();

    PreferencesPtr prefs = Preferences::GetInstance();
    bool origOverlay = prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, true);

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();
    QuiverUtils::ToggleActionSetActive("ViewFilmStrip", TRUE);
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_4k.jpg");
    files.push_back(imgDir + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);

    GtkWidget *fsWidget = viewer->GetFilmstripWidget();
    REQUIRE(fsWidget != nullptr);

    viewer->ShowFilmstripOverlay();

    // Trigger pointer motion on image view to reveal HUD bar
    GtkWidget *imgView = viewer->GetImageView();
    GListModel *controllers = gtk_widget_observe_controllers(imgView);
    for (guint i = 0; i < g_list_model_get_n_items(controllers); i++)
    {
        GObject *obj = (GObject*)g_list_model_get_item(controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_MOTION(obj))
        {
            g_signal_emit_by_name(obj, "motion", 10.0, 10.0);
        }
        g_object_unref(obj);
    }
    g_object_unref(controllers);

    while (g_main_context_iteration(NULL, FALSE));
    REQUIRE(gtk_widget_get_visible(bar));
    REQUIRE(gtk_widget_get_visible(fsWidget));

    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, origOverlay);
    viewer->StopVideo(false);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
}

TEST_CASE("Viewer ResetIdleCursor and Save Dialog Prompt callbacks", "[unit][viewer][cursor]")
{
    REQUIRE_DISPLAY();

    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);

    boost::shared_ptr<Viewer> viewer(new Viewer());
    GtkWidget *viewerWidget = viewer->GetWidget();
    gtk_window_set_child(GTK_WINDOW(win), viewerWidget);

    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    // Test ResetIdleCursor unhides cursor and resets root cursor to NULL (default)
    viewer->ResetIdleCursor();
    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(viewer->GetOverlay()));
    REQUIRE(root != nullptr);
    REQUIRE(gtk_widget_get_cursor(root) == NULL);

    viewer->RefreshAutoHideTimer();

    // Verify Save prompt dialog button callback behavior (preventing crash)
    struct PromptData {
        gint response;
        gboolean neverAsk;
        GMainLoop *loop;
    };

    // 1. Test Discard button callback
    {
        PromptData data = { -1, FALSE, g_main_loop_new(NULL, FALSE) };
        GtkWidget *btnDiscard = gtk_button_new_with_label("Discard Changes");
        g_signal_connect(btnDiscard, "clicked",
            G_CALLBACK(+[](GtkButton *, gpointer ud) {
                auto *d = static_cast<PromptData*>(ud);
                d->response = 1;
                g_main_loop_quit(d->loop);
            }), &data);

        g_idle_add(+[](gpointer btn) -> gboolean {
            g_signal_emit_by_name(btn, "clicked");
            return G_SOURCE_REMOVE;
        }, btnDiscard);
        g_main_loop_run(data.loop);
        g_main_loop_unref(data.loop);

        REQUIRE(data.response == 1);
        g_object_ref_sink(btnDiscard);
        g_object_unref(btnDiscard);
    }

    // 2. Test Save button callback
    {
        PromptData data = { -1, FALSE, g_main_loop_new(NULL, FALSE) };
        GtkWidget *btnSave = gtk_button_new_with_label("Save");
        g_signal_connect(btnSave, "clicked",
            G_CALLBACK(+[](GtkButton *, gpointer ud) {
                auto *d = static_cast<PromptData*>(ud);
                d->response = 0;
                g_main_loop_quit(d->loop);
            }), &data);

        g_idle_add(+[](gpointer btn) -> gboolean {
            g_signal_emit_by_name(btn, "clicked");
            return G_SOURCE_REMOVE;
        }, btnSave);
        g_main_loop_run(data.loop);
        g_main_loop_unref(data.loop);

        REQUIRE(data.response == 0);
        g_object_ref_sink(btnSave);
        g_object_unref(btnSave);
    }

    // 3. Test Cancel button callback
    {
        PromptData data = { -1, FALSE, g_main_loop_new(NULL, FALSE) };
        GtkWidget *btnCancel = gtk_button_new_with_label("Cancel");
        g_signal_connect(btnCancel, "clicked",
            G_CALLBACK(+[](GtkButton *, gpointer ud) {
                auto *d = static_cast<PromptData*>(ud);
                d->response = 2;
                g_main_loop_quit(d->loop);
            }), &data);

        g_idle_add(+[](gpointer btn) -> gboolean {
            g_signal_emit_by_name(btn, "clicked");
            return G_SOURCE_REMOVE;
        }, btnCancel);
        g_main_loop_run(data.loop);
        g_main_loop_unref(data.loop);

        REQUIRE(data.response == 2);
        g_object_ref_sink(btnCancel);
        g_object_unref(btnCancel);
    }

    // 4. Test UndoStackDropRotate
    QuiverFileOps::UndoStackClear();
    QuiverFileOps::UndoStackRecordRotate("file:///tmp/test.jpg", +1);
    REQUIRE(QuiverFileOps::UndoStackHasItems());
    REQUIRE(QuiverFileOps::UndoStackTopType() == QuiverFileOps::UNDO_TYPE_ROTATE);
    QuiverFileOps::UndoStackDropRotate("file:///tmp/test.jpg");
    REQUIRE_FALSE(QuiverFileOps::UndoStackHasItems());

    viewer->StopVideo(false);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
}

TEST_CASE("QuiverRotatedPaintable functionality", "[unit][viewer][paintable]")
{
    REQUIRE_DISPLAY();

    std::string imgDir = QuiverTest_GetImagesDir();
    GdkTexture *tex = gdk_texture_new_from_filename((imgDir + "/sample_4k.jpg").c_str(), NULL);
    REQUIRE(tex != nullptr);

    int origW = gdk_texture_get_width(tex);
    int origH = gdk_texture_get_height(tex);
    REQUIRE(origW > 0);
    REQUIRE(origH > 0);

    QuiverRotatedPaintable *rp = quiver_rotated_paintable_new(GDK_PAINTABLE(tex));
    REQUIRE(rp != nullptr);
    REQUIRE(quiver_rotated_paintable_get_rotation(rp) == 0);
    REQUIRE(gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(rp)) == origW);
    REQUIRE(gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(rp)) == origH);

    // 90 degrees clockwise
    quiver_rotated_paintable_set_rotation(rp, 90);
    REQUIRE(quiver_rotated_paintable_get_rotation(rp) == 90);
    REQUIRE(gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(rp)) == origH);
    REQUIRE(gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(rp)) == origW);

    // 180 degrees
    quiver_rotated_paintable_set_rotation(rp, 180);
    REQUIRE(quiver_rotated_paintable_get_rotation(rp) == 180);
    REQUIRE(gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(rp)) == origW);
    REQUIRE(gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(rp)) == origH);

    // 270 degrees
    quiver_rotated_paintable_set_rotation(rp, 270);
    REQUIRE(quiver_rotated_paintable_get_rotation(rp) == 270);
    REQUIRE(gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(rp)) == origH);
    REQUIRE(gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(rp)) == origW);

    // Normalization check: -90 degrees -> 270 degrees
    quiver_rotated_paintable_set_rotation(rp, -90);
    REQUIRE(quiver_rotated_paintable_get_rotation(rp) == 270);

    // 360 degrees -> 0
    quiver_rotated_paintable_set_rotation(rp, 360);
    REQUIRE(quiver_rotated_paintable_get_rotation(rp) == 0);

    // Snapshot testing
    GtkSnapshot *snap = gtk_snapshot_new();
    quiver_rotated_paintable_set_rotation(rp, 90);
    gdk_paintable_snapshot(GDK_PAINTABLE(rp), snap, origH, origW);
    GskRenderNode *node = gtk_snapshot_free_to_node(snap);
    REQUIRE(node != nullptr);
    gsk_render_node_unref(node);

    // get_current_image testing
    GdkPaintable *cur = gdk_paintable_get_current_image(GDK_PAINTABLE(rp));
    REQUIRE(cur != nullptr);
    REQUIRE(QUIVER_IS_ROTATED_PAINTABLE(cur));
    REQUIRE(quiver_rotated_paintable_get_rotation(QUIVER_ROTATED_PAINTABLE(cur)) == 90);
    g_object_unref(cur);

    g_object_unref(rp);
    g_object_unref(tex);
}

TEST_CASE("Viewer video rotation functionality", "[unit][viewer][video]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    REQUIRE(viewer != nullptr);
    REQUIRE(viewer->GetVideoUserRotation() == 0);

    // Rotate clockwise: 0 -> 90 -> 180 -> 270 -> 0
    viewer->RotateVideo(true);
    CHECK(viewer->GetVideoUserRotation() == 90);

    viewer->RotateVideo(true);
    CHECK(viewer->GetVideoUserRotation() == 180);

    viewer->RotateVideo(true);
    CHECK(viewer->GetVideoUserRotation() == 270);

    viewer->RotateVideo(true);
    CHECK(viewer->GetVideoUserRotation() == 0);

    // Rotate counter-clockwise: 0 -> 270 -> 180
    viewer->RotateVideo(false);
    CHECK(viewer->GetVideoUserRotation() == 270);

    viewer->RotateVideo(false);
    CHECK(viewer->GetVideoUserRotation() == 180);

    // StopVideo resets rotation
    viewer->StopVideo(false);
    CHECK(viewer->GetVideoUserRotation() == 0);

    viewer.reset();
}

TEST_CASE("Viewer center play button scrollwheel navigation", "[unit][viewer][playbtn]")
{
    REQUIRE_DISPLAY();

    GtkWidget *win = gtk_window_new();
    boost::shared_ptr<Viewer> viewer(new Viewer());
    GtkWidget *vw = viewer->GetWidget();
    gtk_window_set_child(GTK_WINDOW(win), vw);
    viewer->Show();
    gtk_window_present(GTK_WINDOW(win));

    GtkWidget *playBtn = viewer->GetCenterPlayButton();
    REQUIRE(playBtn != nullptr);

    // Verify playBtn has a GtkEventControllerScroll attached
    GtkEventControllerScroll *scrollCtrl = nullptr;
    GListModel *controllers = gtk_widget_observe_controllers(playBtn);
    REQUIRE(controllers != nullptr);
    for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
    {
        GObject *c = G_OBJECT(g_list_model_get_item(controllers, i));
        if (GTK_IS_EVENT_CONTROLLER_SCROLL(c))
        {
            scrollCtrl = GTK_EVENT_CONTROLLER_SCROLL(c);
            g_object_unref(c);
            break;
        }
        g_object_unref(c);
    }
    g_object_unref(controllers);
    REQUIRE(scrollCtrl != nullptr);

    // Setup an ImageList with two items
    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_4k.jpg");
    files.push_back(imgDir + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    while (g_main_context_iteration(NULL, FALSE));

    REQUIRE(list->GetCurrentIndex() == 0);

    // Scroll forward (dy = 1.0) on the play button controller
    gboolean handled = FALSE;
    g_signal_emit_by_name(scrollCtrl, "scroll", 0.0, 1.0, &handled);
    CHECK(handled == TRUE);
    CHECK(list->GetCurrentIndex() == 1);

    // Scroll backward (dy = -1.0) on the play button controller
    handled = FALSE;
    g_signal_emit_by_name(scrollCtrl, "scroll", 0.0, -1.0, &handled);
    CHECK(handled == TRUE);
    CHECK(list->GetCurrentIndex() == 0);

    viewer->StopVideo(false);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
}

TEST_CASE("Video Glitch Detector initialization and real-time monitoring", "[unit][viewer][glitch]")
{
    REQUIRE_DISPLAY();

    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);

    boost::shared_ptr<Viewer> viewer(new Viewer());
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget *playBtn = viewer->GetCenterPlayButton();
    REQUIRE(playBtn != nullptr);
    g_signal_emit_by_name(playBtn, "clicked");

    for (int i = 0; i < 20; ++i)
    {
        g_usleep(15000);
        while (g_main_context_iteration(NULL, FALSE));
    }

    std::ifstream log_file("quiver_glitch_log.txt");
    REQUIRE(log_file.good());

    viewer->StopVideo(false);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
}

TEST_CASE("Viewer Video Kinetic Scrolling and Pan Deceleration", "[unit][viewer][video][kinetic]")
{
    REQUIRE_DISPLAY();

    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != nullptr);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, true);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);

    boost::shared_ptr<Viewer> viewer(new Viewer());
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    ImageListPtr list(new ImageList());
    std::string imgDir = QuiverTest_GetImagesDir();
    std::list<std::string> files;
    files.push_back(imgDir + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget *playBtn = viewer->GetCenterPlayButton();
    REQUIRE(playBtn != nullptr);
    g_signal_emit_by_name(playBtn, "clicked");

    for (int i = 0; i < 20; ++i)
    {
        g_usleep(15000);
        while (g_main_context_iteration(NULL, FALSE));
    }

    // Initially at fit zoom: video should not be actively panning
    CHECK_FALSE(viewer->IsVideoPanSlowdownActive());

    // Zoom in to 2.0x so video overflows the viewport and becomes pannable
    viewer->SetVideoZoom(2.0);
    for (int i = 0; i < 30 && viewer->GetVideoZoom() < 1.95; ++i)
    {
        g_usleep(35000);
        while (g_main_context_iteration(NULL, FALSE));
    }

    CHECK(viewer->CanVideoPan() == true);
    CHECK(viewer->GetVideoZoom() > 1.0);

    // Test 1: Record flick samples and start kinetic slowdown
    // Simulate 3 motion samples at ~60fps (dt = 0.016s) moving right (dx = 20px)
    viewer->RecordVideoPanSample(20.0, 0.0, 0.016);
    viewer->RecordVideoPanSample(22.0, 0.0, 0.016);
    viewer->RecordVideoPanSample(18.0, 0.0, 0.016);

    viewer->StartVideoPanSlowdown();
    CHECK(viewer->IsVideoPanSlowdownActive() == true);

    // Initial velocity should be ~1250 px/s
    double initialVx = viewer->GetVideoPanVelocityX();
    CHECK(initialVx > 1000.0);
    CHECK(initialVx < 1500.0);
    CHECK(viewer->GetVideoPanVelocityY() == 0.0);

    // Record initial pan X
    double panX0 = viewer->GetVideoPanX();

    // Iterate the main loop to drive the slowdown animation steps
    for (int i = 0; i < 10; ++i)
    {
        g_usleep(16000);
        while (g_main_context_iteration(NULL, FALSE));
    }

    // Velocity should have decayed exponentially (k ≈ 1.1206 s^-1)
    double decayedVx = viewer->GetVideoPanVelocityX();
    CHECK(decayedVx < initialVx);
    CHECK(decayedVx > 0.0);

    // Pan position should have shifted to follow the flick direction
    double panX1 = viewer->GetVideoPanX();
    CHECK(panX1 != panX0);

    // Test 2: Stopping slowdown directly
    viewer->StopVideoPanSlowdown();
    CHECK_FALSE(viewer->IsVideoPanSlowdownActive());
    CHECK(viewer->GetVideoPanVelocityX() == 0.0);
    CHECK(viewer->GetVideoPanVelocityY() == 0.0);

    // Test 3: Preference toggle disables kinetic slowdown
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, false);
    while (g_main_context_iteration(NULL, FALSE));

    viewer->RecordVideoPanSample(20.0, 0.0, 0.016);
    viewer->RecordVideoPanSample(20.0, 0.0, 0.016);
    viewer->StartVideoPanSlowdown();
    CHECK_FALSE(viewer->IsVideoPanSlowdownActive());

    // Restore preference
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, true);
    while (g_main_context_iteration(NULL, FALSE));

    // Test 4: Low speed below threshold (< 15 px/s) should not trigger slowdown
    viewer->RecordVideoPanSample(0.1, 0.0, 0.016); // ~6 px/s
    viewer->StartVideoPanSlowdown();
    CHECK_FALSE(viewer->IsVideoPanSlowdownActive());

    // Test 5: Clicking the mouse while kinetic scroll is in progress stops the scroll without toggling play/pause
    GtkWidget *overlay = viewer->GetOverlay();
    REQUIRE(overlay != nullptr);
    GtkWidget *stack = gtk_overlay_get_child(GTK_OVERLAY(overlay));
    REQUIRE(stack != nullptr);
    GtkWidget *videoFixed = gtk_stack_get_child_by_name(GTK_STACK(stack), "video");
    REQUIRE(videoFixed != nullptr);

    GListModel *controllers = gtk_widget_observe_controllers(videoFixed);
    REQUIRE(controllers != nullptr);
    GtkGestureClick *clickGesture = nullptr;
    for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
    {
        GObject *obj = G_OBJECT(g_list_model_get_item(controllers, i));
        if (GTK_IS_GESTURE_CLICK(obj))
        {
            clickGesture = GTK_GESTURE_CLICK(obj);
            g_object_unref(obj);
            break;
        }
        g_object_unref(obj);
    }
    g_object_unref(controllers);
    REQUIRE(clickGesture != nullptr);

    // Start kinetic scroll again
    viewer->RecordVideoPanSample(25.0, 0.0, 0.016);
    viewer->RecordVideoPanSample(25.0, 0.0, 0.016);
    viewer->StartVideoPanSlowdown();
    CHECK(viewer->IsVideoPanSlowdownActive() == true);

    bool wasPlaying = viewer->IsVideoPlaying();

    // User clicks mouse on video: press at (100, 100)
    g_signal_emit_by_name(clickGesture, "pressed", 1, 100.0, 100.0);
    // Slowdown should be stopped immediately
    CHECK_FALSE(viewer->IsVideoPanSlowdownActive());

    // Mouse button released at (100, 100) without drag
    g_signal_emit_by_name(clickGesture, "released", 1, 100.0, 100.0);

    // Playing state must NOT have changed!
    CHECK(viewer->IsVideoPlaying() == wasPlaying);

    // Now, clicking again when slowdown is NOT in progress SHOULD toggle play/pause
    g_signal_emit_by_name(clickGesture, "pressed", 1, 100.0, 100.0);
    g_signal_emit_by_name(clickGesture, "released", 1, 100.0, 100.0);
    CHECK(viewer->IsVideoPlaying() != wasPlaying);

    // Test 6: Clicking over video control overlay does NOT stop kinetic scroll slowdown
    GtkWidget *overlayBar = nullptr;
    for (GtkWidget *child = gtk_widget_get_first_child(overlay); child != nullptr; child = gtk_widget_get_next_sibling(child))
    {
        if (GTK_IS_BOX(child) && gtk_widget_has_css_class(child, "viewer-overlay-bar"))
        {
            overlayBar = child;
            break;
        }
    }

    REQUIRE(overlayBar != nullptr);
    gtk_widget_set_visible(overlayBar, TRUE);
    gtk_widget_set_opacity(overlayBar, 1.0);
    gtk_widget_set_size_request(overlayBar, 300, 40);
    while (g_main_context_iteration(NULL, FALSE));

    int barW = gtk_widget_get_width(overlayBar);
    int barH = gtk_widget_get_height(overlayBar);
    if (barW <= 0) barW = 300;
    if (barH <= 0) barH = 40;
    {
        graphene_point_t bar_center = GRAPHENE_POINT_INIT((float)barW / 2.0f, (float)barH / 2.0f);
            graphene_point_t video_pt;
            if (gtk_widget_compute_point(overlayBar, videoFixed, &bar_center, &video_pt))
            {
                // Point inside overlay bar must be detected as over controls
                CHECK(viewer->IsPointOverControlsOrFilmstrip((double)video_pt.x, (double)video_pt.y) == true);
                // Point in viewer canvas area must NOT be detected as over controls
                CHECK(viewer->IsPointOverControlsOrFilmstrip(100.0, 100.0) == false);

                // Start kinetic scroll slowdown
                viewer->RecordVideoPanSample(25.0, 0.0, 0.016);
                viewer->RecordVideoPanSample(25.0, 0.0, 0.016);
                viewer->StartVideoPanSlowdown();
                CHECK(viewer->IsVideoPanSlowdownActive() == true);

                // Click over control overlay: press and release
                g_signal_emit_by_name(clickGesture, "pressed", 1, (double)video_pt.x, (double)video_pt.y);
                // Slowdown must NOT be stopped!
                CHECK(viewer->IsVideoPanSlowdownActive() == true);

                g_signal_emit_by_name(clickGesture, "released", 1, (double)video_pt.x, (double)video_pt.y);
                CHECK(viewer->IsVideoPanSlowdownActive() == true);

                // Now click over the viewer canvas area (100, 100) -> MUST stop slowdown!
                g_signal_emit_by_name(clickGesture, "pressed", 1, 100.0, 100.0);
                CHECK_FALSE(viewer->IsVideoPanSlowdownActive());
                g_signal_emit_by_name(clickGesture, "released", 1, 100.0, 100.0);
            }
        }

    // Test 7: Cleanup
    viewer->StopVideo(false);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
}


// The viewer registers its actions in the global action group, which lives
// as long as the application.  While a slideshow runs, UpdateUI() activates
// "MaximizeForDisplay" - if a previous viewer had registered it and was
// destroyed without removing it, that activation ran the old viewer's
// handler on a freed object and crashed inside gtk_widget_set_visible().
TEST_CASE("Viewer actions do not outlive the viewer", "[unit][viewer][actions]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();
    REQUIRE(QuiverUtils::GetAction("ZoomIn") != nullptr);
    REQUIRE(QuiverUtils::GetAction("MaximizeForDisplay") != nullptr);

    viewer.reset();

    CHECK(QuiverUtils::GetAction("ZoomIn") == nullptr);
    CHECK(QuiverUtils::GetAction("MaximizeForDisplay") == nullptr);

    // a new viewer registers them again, so a second slideshow in the same
    // process still finds the actions it needs
    boost::shared_ptr<Viewer> second(new Viewer());
    second->RegisterActions();
    CHECK(QuiverUtils::GetAction("MaximizeForDisplay") != nullptr);
    second.reset();
}

/* Opaque solid-colour texture, so each test image can have its own size. */
static GdkTexture *make_flat_texture(int w, int h, guchar r, guchar g, guchar b)
{
    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
    if (pb == nullptr)
    {
        return nullptr;
    }
    gdk_pixbuf_fill(pb, ((guint)r << 24) | ((guint)g << 16) | ((guint)b << 8));
    GdkTexture *tex = gdk_texture_new_for_pixbuf(pb);
    g_object_unref(pb);
    return tex;
}

TEST_CASE("Viewer keep zoom and pan preserves the image centre", "[unit][viewer][zoomkeep]")
{
    REQUIRE_DISPLAY();

    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    GtkWidget *iv = quiver_image_view_new();
    gtk_window_set_child(GTK_WINDOW(win), iv);
    gtk_window_present(GTK_WINDOW(win));
    for (int i = 0; (i < 200) && (gtk_widget_get_width(iv) <= 1); ++i)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(5000);
    }
    REQUIRE(gtk_widget_get_width(iv) > 1);
    REQUIRE(gtk_widget_get_height(iv) > 1);

    QuiverImageView *view = QUIVER_IMAGE_VIEW(iv);
    GtkAdjustment *hadj = quiver_image_view_get_hadjustment(view);
    GtkAdjustment *vadj = quiver_image_view_get_vadjustment(view);
    REQUIRE(hadj != nullptr);
    REQUIRE(vadj != nullptr);

    /* the point of the picture under the middle of the viewport, as a fraction
     * of the picture: that, not a count of pixels, is what has to survive an
     * image change - a pixel offset of one picture is not a position in the
     * next one, and off its edge there is nothing to scroll to but the middle */
    auto centre = [](GtkAdjustment *adj, gdouble mag, gdouble picture_size) {
        return (gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj) / 2.0
                - gtk_adjustment_get_lower(adj)) / mag / picture_size;
    };

    GdkTexture *first = make_flat_texture(400, 400, 200, 30, 30);
    REQUIRE(first != nullptr);
    quiver_image_view_set_texture_at_size_ex(view, first, 400, 400, TRUE);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    quiver_image_view_set_magnification(view, 2.0);
    REQUIRE(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));

    /* pan away from the middle so a broken implementation cannot pass by
     * simply keeping the default scroll position */
    REQUIRE(gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj) > 1.0);
    gtk_adjustment_set_value(hadj, 100.0);
    gtk_adjustment_set_value(vadj, 60.0);
    REQUIRE(gtk_adjustment_get_value(hadj) == Catch::Approx(100.0));

    const double centre_x = centre(hadj, 2.0, 400.0);
    const double centre_y = centre(vadj, 2.0, 400.0);
    REQUIRE(centre_x > 0.05);
    REQUIRE(centre_y > 0.05);
    REQUIRE(centre_x < 0.95);
    REQUIRE(centre_y < 0.95);
    /* off the middle, so a view that merely forgot the pan cannot pass */
    REQUIRE(std::abs(centre_x - 0.5) > 0.05);

    /* next image has a different size in both directions */
    GdkTexture *second = make_flat_texture(600, 800, 30, 30, 200);
    REQUIRE(second != nullptr);
    quiver_image_view_set_texture_at_size_ex(view, second, 600, 800, FALSE);

    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));
    CHECK(centre(hadj, 2.0, 600.0) == Catch::Approx(centre_x).margin(0.01));
    CHECK(centre(vadj, 2.0, 800.0) == Catch::Approx(centre_y).margin(0.01));

    /* A reset puts the picture back where the mode in force shows it and leaves
     * that mode alone: 1:1 goes back to actual size, the fit modes back to the
     * fit, both centred. */
    quiver_image_view_set_texture_at_size_ex(view, first, 400, 400, TRUE);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM);
    quiver_image_view_set_magnification(view, 2.0);
    REQUIRE(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));
    gtk_adjustment_set_value(hadj, 100.0);
    quiver_image_view_set_texture_at_size_ex(view, second, 600, 800, TRUE);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(1.0));
    double reset_x = 0., reset_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &reset_x, &reset_y));
    CHECK(reset_x == Catch::Approx(0.5).margin(0.01));
    CHECK(reset_y == Catch::Approx(0.5).margin(0.01));

    /* A fit mode has no zoom to undo - it shows every picture the way it shows
     * them all - so a reset leaves it doing that, in the same mode. */
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
    quiver_image_view_set_texture_at_size_ex(view, first, 400, 400, TRUE);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
    gint fit_width = 0, fit_height = 0;
    quiver_image_view_get_pixbuf_display_size_for_mode(view, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH,
        &fit_width, &fit_height);
    CHECK(quiver_image_view_get_magnification(view)
        == Catch::Approx((gdouble)fit_width / 400.0).margin(0.001));

    /* and "keep zoom and pan" is not a zoom to undo: the zoom and the pan
     * survive, because they are the one thing that mode is for */
    quiver_image_view_set_texture_at_size_ex(view, first, 400, 400, TRUE);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    quiver_image_view_set_magnification(view, 2.0);
    gtk_adjustment_set_value(hadj, 100.0);
    REQUIRE(quiver_image_view_get_view_center(view, &reset_x, &reset_y));
    quiver_image_view_set_texture_at_size_ex(view, second, 600, 800, TRUE);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));
    double kept_x = 0., kept_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &kept_x, &kept_y));
    CHECK(kept_x == Catch::Approx(reset_x).margin(0.02));
    CHECK(kept_y == Catch::Approx(reset_y).margin(0.02));

    g_object_unref(first);
    g_object_unref(second);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
}

TEST_CASE("Viewer HUD view mode split button", "[unit][viewer][overlay][zoomkeep]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    /* the arrow is only enabled once there is something to view */
    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);

    GtkWidget *split = nullptr;
    GtkWidget *arrow = nullptr;
    GtkWidget *fit = nullptr;

    auto walk = [&](auto &self, GtkWidget *parent) -> void {
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
        {
            const char *tip = gtk_widget_get_tooltip_text(child);
            if ((tip != nullptr) && (std::string(tip) == "View Mode"))
            {
                split = gtk_widget_get_parent(child);
                arrow = child;
            }
            else if ((tip != nullptr) && (std::string(tip).find("Fit") != std::string::npos))
            {
                fit = child;
            }
            self(self, child);
        }
    };
    walk(walk, bar);

    REQUIRE(arrow != nullptr);
    /* a plain button, not a GtkMenuButton: a menu button draws an arrow of
     * its own next to whatever icon it is given and keeps a 36x34 minimum
     * that no CSS overrides, which left it as wide as a full HUD button */
    REQUIRE(GTK_IS_BUTTON(arrow));
    /* the arrow is deliberately not a .media-btn: that class makes every HUD
     * button a square 2.4em box, which is far too wide for a bare arrow */
    REQUIRE_FALSE(gtk_widget_has_css_class(arrow, "media-btn"));
    REQUIRE(gtk_widget_has_css_class(arrow, "view-mode-arrow"));

    /* the fit action stays as the main half of the split button */
    REQUIRE(split != nullptr);
    REQUIRE(GTK_IS_BOX(split));
    REQUIRE(gtk_widget_has_css_class(split, "view-mode-split"));
    REQUIRE(gtk_widget_get_first_child(split) == fit);
    REQUIRE(GTK_IS_BUTTON(fit));
    REQUIRE(gtk_widget_has_css_class(fit, "media-btn"));
    REQUIRE(gtk_widget_get_parent(fit) == split);

    /* the action behind the popover is registered for this viewer */
    REQUIRE(QuiverUtils::GetAction("ZoomKeep") != nullptr);

    /* the arrow owns a popover offering every view mode */
    GtkWidget *popover = viewer->GetViewModeMenuPopover();
    REQUIRE(popover != nullptr);
    REQUIRE(GTK_IS_POPOVER_MENU(popover));
    REQUIRE(gtk_widget_get_parent(popover) == arrow);
    GMenuModel *model = gtk_popover_menu_get_menu_model(GTK_POPOVER_MENU(popover));
    REQUIRE(model != nullptr);
    CHECK(g_menu_model_get_n_items(model) == 5);
    CHECK(gtk_widget_get_parent(popover) != nullptr);

    /* the whole control is hovered as one button: a motion controller on the
     * box adds the class the CSS highlight hangs off, and the two halves hang
     * off the box together */
    REQUIRE(!gtk_widget_has_css_class(split, "view-mode-split-hovered"));
    GtkEventController *hover = nullptr;
    GListModel *controllers = gtk_widget_observe_controllers(split);
    for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
    {
        GObject *candidate = G_OBJECT(g_list_model_get_item(controllers, i));
        if (GTK_IS_EVENT_CONTROLLER_MOTION(candidate))
        {
            hover = GTK_EVENT_CONTROLLER(candidate);
            break;
        }
        g_object_unref(candidate);
    }
    REQUIRE(hover != nullptr);
    g_signal_emit_by_name(hover, "enter", 1.0, 1.0);
    CHECK(gtk_widget_has_css_class(split, "view-mode-split-hovered"));
    g_signal_emit_by_name(hover, "leave");
    CHECK(!gtk_widget_has_css_class(split, "view-mode-split-hovered"));

    /* the separators between the HUD's button groups use the same thin
     * separator as the split control's own one */
    int hud_seps = 0;
    for (GtkWidget *child = gtk_widget_get_first_child(bar); child != nullptr; child = gtk_widget_get_next_sibling(child))
    {
        if (GTK_IS_SEPARATOR(child) && gtk_widget_has_css_class(child, "hud-sep"))
            hud_seps++;
    }
    CHECK(hud_seps == 3);

    /* the arrow hugs its 12px icon and the separator between the halves is
     * half the button's height, both of which the HUD is measured against */
    GtkWidget *split_sep = gtk_widget_get_first_child(split);
    while (split_sep != nullptr && !GTK_IS_SEPARATOR(split_sep))
        split_sep = gtk_widget_get_next_sibling(split_sep);
    REQUIRE(split_sep != nullptr);
    int fit_h = 0, fit_nat = 0, fit_min_b = 0, fit_nat_b = 0;
    gtk_widget_measure(fit, GTK_ORIENTATION_VERTICAL, -1, &fit_h, &fit_nat, &fit_min_b, &fit_nat_b);
    int arrow_w = 0, arrow_nat = 0, arrow_min_b = 0, arrow_nat_b = 0;
    gtk_widget_measure(arrow, GTK_ORIENTATION_HORIZONTAL, -1, &arrow_w, &arrow_nat, &arrow_min_b, &arrow_nat_b);
    int sep_h = 0, sep_nat = 0, sep_min_b = 0, sep_nat_b = 0;
    gtk_widget_measure(split_sep, GTK_ORIENTATION_VERTICAL, -1, &sep_h, &sep_nat, &sep_min_b, &sep_nat_b);
    CHECK(arrow_w > 0);
    CHECK(arrow_w < fit_h / 2);
    CHECK(gtk_widget_get_valign(split_sep) == GTK_ALIGN_CENTER);
    CHECK(sep_h == fit_h / 2);
    /* the group's own separators still fill the button height */
    GtkWidget *group_sep = gtk_widget_get_first_child(bar);
    bool saw_group_sep = false;
    for (; group_sep != nullptr; group_sep = gtk_widget_get_next_sibling(group_sep))
    {
        if (GTK_IS_SEPARATOR(group_sep) && gtk_widget_has_css_class(group_sep, "hud-sep"))
        {
            saw_group_sep = true;
            CHECK(gtk_widget_get_valign(group_sep) == GTK_ALIGN_FILL);
            break;
        }
    }
    CHECK(saw_group_sep);

    /* a new viewer registers it again, so a second slideshow in the same
     * process still finds the action it needs */
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
    viewer.reset();
    CHECK(QuiverUtils::GetAction("ZoomKeep") == nullptr);
}

TEST_CASE("Viewer view modes stay available while zoomed", "[unit][viewer][zoomkeep]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));
    for (int i = 0; i < 200; ++i)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(2000);
    }

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);

    /* zoomed: "fit to window" is a no-op now, but the action is the same one
     * the view-mode popover shows, and offering it is the whole point of the
     * popover - it must not be disabled along with the button */
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
    quiver_image_view_set_magnification(view, 2.0);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    while (g_main_context_iteration(NULL, FALSE));

    GAction *fit = QuiverUtils::GetAction("ZoomFit");
    REQUIRE(fit != nullptr);
    CHECK(g_action_get_enabled(G_ACTION(fit)));
    CHECK(g_action_get_enabled(G_ACTION(QuiverUtils::GetAction("ZoomKeep"))));
    CHECK(g_action_get_enabled(G_ACTION(QuiverUtils::GetAction("ZoomFitStretch"))));
    CHECK(g_action_get_enabled(G_ACTION(QuiverUtils::GetAction("Zoom100"))));
    CHECK(g_action_get_enabled(G_ACTION(QuiverUtils::GetAction("ZoomFillScreen"))));

    /* the fit button itself does go insensitive, it would change nothing */
    GAction *zoom_fit_btn = QuiverUtils::GetAction("ZoomFit");
    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);
    GAction *in = QuiverUtils::GetAction("ZoomIn");
    CHECK(g_action_get_enabled(G_ACTION(in)));

    viewer.reset();
}

TEST_CASE("Viewer HUD keeps its layout across image and video", "[unit][viewer][overlay][video]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    std::string dir = QuiverTest_GetImagesDir();
    files.push_back(dir + "/sample_4k.jpg");
    files.push_back(dir + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 900, 700);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = []() {
        for (int i = 0; i < 300; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();

    int video_index = -1;
    for (guint i = 0; i < list->GetSize(); ++i)
    {
        if (list->Get(i).IsVideo())
            video_index = (int)i;
    }
    REQUIRE(video_index >= 0);
    REQUIRE(list->SetCurrentIndex((unsigned int)video_index));
    settle();

    GtkWidget *bar = viewer->GetViewerOverlayBar();
    REQUIRE(bar != nullptr);

    /* the fit/mode split control belongs to the HUD in both modes */
    GtkWidget *split = nullptr;
    GtkWidget *arrow = nullptr;
    auto find_arrow = [&](auto &self, GtkWidget *parent) -> void {
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
        {
            const char *tip = gtk_widget_get_tooltip_text(child);
            if ((tip != nullptr) && (std::string(tip) == "View Mode"))
            {
                arrow = child;
                split = gtk_widget_get_parent(child);
            }
            self(self, child);
        }
    };
    find_arrow(find_arrow, bar);
    REQUIRE(arrow != nullptr);
    REQUIRE(split != nullptr);
    CHECK(gtk_widget_get_visible(split));
    CHECK(gtk_widget_get_sensitive(arrow));

    /* Every slot of the bar holds one widget, and the HUD swaps which one it
     * is (blank for play, rotate for skip, ...), so the buttons around the
     * split control must land on exactly the same pixels in both modes. */
    std::vector<std::pair<std::string, int>> image_layout;
    std::vector<std::pair<std::string, int>> video_layout;
    /* x relative to the bar, so the nested halves of the split control are
     * comparable with the plain buttons next to them */
    auto measure = [&](auto &self, GtkWidget *parent, int offset, std::vector<std::pair<std::string, int>> &out) -> void {
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child != nullptr; child = gtk_widget_get_next_sibling(child))
        {
            GtkAllocation alloc;
            gtk_widget_get_allocation(child, &alloc);
            const char *tip = gtk_widget_get_tooltip_text(child);
            if (tip != nullptr)
                out.emplace_back(tip, offset + alloc.x);
            self(self, child, offset + alloc.x, out);
        }
    };
    measure(measure, bar, 0, image_layout);
    REQUIRE(!image_layout.empty());
    bool found_arrow = false;
    for (const auto &entry : image_layout)
    {
        if (entry.first == "View Mode")
            found_arrow = true;
    }
    REQUIRE(found_arrow);

    REQUIRE(list->SetCurrentIndex(0));
    settle();
    measure(measure, bar, 0, video_layout);
    REQUIRE(video_layout.size() == image_layout.size());
    for (size_t i = 0; i < image_layout.size(); ++i)
    {
        CHECK(video_layout[i].first == image_layout[i].first);
        CHECK(video_layout[i].second == image_layout[i].second);
    }

    viewer.reset();
}

TEST_CASE("Viewer shows a video in the viewer's view mode", "[unit][viewer][zoomkeep][video]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_rotated.jpg");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 200) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);

    GAction *zoom_in = QuiverUtils::GetAction("ZoomIn");
    REQUIRE(zoom_in != nullptr);

    /* The view mode is a radio group, and QuiverUtils keeps it in a process
     * wide list: this test binary builds several viewers, one after another, so
     * the state the group is in has to be set explicitly before its action is
     * activated - which is what picking the entry in the view mode menu does
     * anyway. */
    auto pick_mode = [&](QuiverImageViewMode mode) {
        QuiverUtils::SetRadioActionCurrent("Zoom", (gint)mode);
        const char *name = (mode == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP) ? "ZoomKeep" : "ZoomFitStretch";
        GAction *action = QuiverUtils::GetAction(name);
        REQUIRE(action != nullptr);
        g_action_activate(action, NULL);
    };

    /* "keep zoom and pan" is picked the way a user picks it - from the view
     * mode menu - and it is the viewer's mode, so the still and the video are
     * both in it */
    pick_mode(QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    settle();
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    quiver_image_view_set_magnification(view, 2.0);
    REQUIRE(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));

    /* the list is sorted, so find the items by what they are */
    int video_index = -1;
    int other_still_index = -1;
    for (guint i = 0; i < list->GetSize(); ++i)
    {
        if (list->Get(i).IsVideo())
            video_index = (int)i;
        else if ((int)i != 0)
            other_still_index = (int)i;
    }
    REQUIRE(video_index > 0);
    REQUIRE(other_still_index > 0);

    /* arriving at the video changes nothing about the mode: it is an item like
     * any other, so "keep zoom and pan" means the same thing for it */
    REQUIRE(list->SetCurrentIndex((unsigned int)video_index));
    REQUIRE(list->GetCurrent().IsVideo());
    settle();
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    /* the pan is a fraction of the frame (0..1), so it means the same thing
     * whatever the frame is - which is what the nav control's zoom box shows */
    CHECK(viewer->GetVideoPanFractionX() >= 0.0);
    CHECK(viewer->GetVideoPanFractionX() <= 1.0);
    CHECK(viewer->GetVideoPanFractionY() >= 0.0);
    CHECK(viewer->GetVideoPanFractionY() <= 1.0);

    /* picking a mode over the video puts the stills in it too, so the mode
     * around a video is never a surprise */
    pick_mode(QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
    settle();
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
    pick_mode(QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    settle();
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    /* Zooming the quick preview grows the preview the image view draws, and
     * leaves the mode alone: the mode says what happens on the *next* item, so
     * a zoom is not allowed to spend it - unselecting "keep zoom and pan" is
     * exactly what the user was trying not to do by zooming. */
    REQUIRE(quiver_image_view_get_texture(view) != nullptr);
    g_action_activate(zoom_in, NULL);
    settle();
    const gdouble first_zoom = quiver_image_view_get_magnification(view);
    g_action_activate(zoom_in, NULL);
    settle();
    const gdouble second_zoom = quiver_image_view_get_magnification(view);
    CHECK(second_zoom > first_zoom);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(viewer->GetVideoPanFractionX() >= 0.0);
    CHECK(viewer->GetVideoPanFractionX() <= 1.0);

    /* and the still after the video is in one and the same mode with the video:
     * there is no second, video-only mode to be surprised by */
    REQUIRE(list->SetCurrentIndex((unsigned int)other_still_index));
    settle(300);
    CHECK(viewer->GetVideoViewMode() == quiver_image_view_get_view_mode(view));

    viewer.reset();
}

TEST_CASE("Viewer shows the nav control for a zoomed video preview", "[unit][viewer][video][zoomkeep]")
{
    REQUIRE_DISPLAY();

    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != nullptr);
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, true);

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    /* the viewer widget is created hidden (the app shows it when it packs it
     * into its own window), and the quick preview is only on screen while the
     * image view is mapped */
    gtk_widget_set_visible(viewer->GetWidget(), TRUE);
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 100) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();

    /* back to the state a video that has not been played is in: the pipeline
     * knows no frame size, so the image view is on screen in its place
     * drawing the video's own thumbnail - the quick preview */
    viewer->StopVideo(true);
    settle();

    GtkWidget *pill = viewer->GetNavControlPill();
    REQUIRE(pill != nullptr);
    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);

    /* the quick preview fits in the window, so there is no viewport to show
     * and the control stays out of the way */
    CHECK(gtk_widget_get_visible(pill) == false);

    /* zoom the preview: the window now shows less than the whole frame, which
     * is what the control is for.  It has to appear while the preview is being
     * framed, before any frame has come out of the pipeline - the preview is
     * drawn by the image view, and so is the control's box. */
    GAction *zoom_in = QuiverUtils::GetAction("ZoomIn");
    REQUIRE(zoom_in != nullptr);
    for (int i = 0; i < 3; ++i)
    {
        g_action_activate(zoom_in, NULL);
        settle(50);
    }
    CHECK(quiver_image_view_get_magnification(view) > 1.0);
    CHECK(gtk_widget_get_visible(pill) == true);

    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, false);
    viewer.reset();
}

TEST_CASE("Viewer plays a video at the zoom and pan its quick preview was left at", "[unit][viewer][video][zoomkeep]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    /* the viewer widget is created hidden (the app shows it when it packs it
     * into its own window), and the quick preview is only on screen while the
     * image view is mapped */
    gtk_widget_set_visible(viewer->GetWidget(), TRUE);
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 100) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();

    viewer->StopVideo(true);
    settle();

    /* frame the quick preview with the zoom buttons, the way a user does */
    GAction *zoom_in = QuiverUtils::GetAction("ZoomIn");
    REQUIRE(zoom_in != nullptr);
    for (int i = 0; i < 3; ++i)
    {
        g_action_activate(zoom_in, NULL);
        settle(50);
    }

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);
    double preview_zoom = viewer->GetVideoZoom();
    double preview_pan_x = viewer->GetVideoPanFractionX();
    double preview_pan_y = viewer->GetVideoPanFractionY();
    double preview_mag = quiver_image_view_get_magnification(view);
    REQUIRE(preview_zoom > 1.0);
    REQUIRE(preview_mag > 1.0);

    /* pressing play loads the uri, which stops the video first - and that stop
     * resets a video to fit.  The framing the user chose on the preview is what
     * playback has to start at, or the picture jumps to a fitted, centered
     * frame the moment it starts */
    GAction *play = QuiverUtils::GetAction("VideoPlay");
    REQUIRE(play != nullptr);
    g_action_activate(play, NULL);
    settle();

    CHECK(viewer->GetVideoZoom() == Catch::Approx(preview_zoom).epsilon(0.01));
    CHECK(viewer->GetVideoPanFractionX() == Catch::Approx(preview_pan_x).epsilon(0.01));
    CHECK(viewer->GetVideoPanFractionY() == Catch::Approx(preview_pan_y).epsilon(0.01));
    /* the preview is still on screen, still borrowed: nothing about the
     * picture changes when play starts, the first frame just takes over at the
     * same framing */
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(preview_mag).epsilon(0.01));

    viewer.reset();
}

TEST_CASE("Viewer shows a zoomed video's quick preview at the zoom it will play at",
          "[unit][viewer][zoomkeep][video]")
{
    REQUIRE_DISPLAY();

    /* The quick preview is drawn by the image view, and a video that arrives
     * while "keep zoom and pan" is in force plays at the zoom the previous
     * video was left at.  The preview has to be put at that same zoom and pan,
     * or the user is shown one framing and gets another on play - and what the
     * image view kept from the previous item cannot stand in for it, because
     * that was a *fitted* preview: the middle of its viewport is not the middle
     * of the frame, so carrying it over lands somewhere else again. */
    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video_4x3.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 200) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    auto index_of = [&](const std::string &name) {
        for (unsigned int i = 0; i < list->GetSize(); ++i)
            if (list->Get(i).GetFilePath() == name)
                return (int)i;
        return -1;
    };
    settle();

    const int first = index_of(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    const int second = index_of(QuiverTest_GetImagesDir() + "/sample_video_4x3.mp4");
    REQUIRE(first >= 0);
    REQUIRE(second >= 0);

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);

    GAction *zoom_keep = QuiverUtils::GetAction("ZoomKeep");
    GAction *zoom_in = QuiverUtils::GetAction("ZoomIn");
    REQUIRE(zoom_keep != nullptr);
    REQUIRE(zoom_in != nullptr);

    REQUIRE(list->SetCurrentIndex((unsigned int)first));
    settle();
    gtk_widget_set_visible(viewer->GetWidget(), TRUE);
    settle();
    REQUIRE(list->GetCurrent().IsVideo());

    QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    g_action_activate(zoom_keep, NULL);
    settle();
    REQUIRE(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    /* zoom the first video well in, and let go of the pan so the frame is off
     * the middle: a preview at the fit zoom would be caught by a test that only
     * checked the middle */
    g_action_activate(zoom_in, NULL);
    settle(20);
    g_action_activate(zoom_in, NULL);
    settle(20);
    /* panned on the quick preview, which is where the user does it: the pan has
     * to reach the video, or play starts somewhere the preview never showed */
    REQUIRE(quiver_image_view_get_texture(view) != nullptr);
    quiver_image_view_set_view_center(view, 0.35, 0.5);
    settle(20);
    /* the pan that was asked for has to be one the viewport can reach, or the
     * test would be asserting a framing the view could not show anyway */
    double shown_cx = 0., shown_cy = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &shown_cx, &shown_cy));
    REQUIRE(fabs(shown_cx - 0.5) > 0.02);

    const double zoom = viewer->GetVideoZoom();
    const double pan_x = viewer->GetVideoPanFractionX();
    const double pan_y = viewer->GetVideoPanFractionY();
    REQUIRE(zoom > 1.5);
    REQUIRE(fabs(pan_x - 0.5) > 0.02);

    /* to the next video, which is a different aspect ratio and arrives with
     * "keep zoom and pan" still in force */
    REQUIRE(list->SetCurrentIndex((unsigned int)second));
    settle(200);
    REQUIRE(list->GetCurrent().IsVideo());
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(viewer->GetVideoZoom() == Catch::Approx(zoom).margin(0.05));
    CHECK(viewer->GetVideoPanFractionX() == Catch::Approx(pan_x).margin(0.05));
    CHECK(viewer->GetVideoPanFractionY() == Catch::Approx(pan_y).margin(0.05));

    /* and the preview on screen is at that zoom, not at the fit zoom it is
     * delivered with: the picture it is showing is the frame's own size, so the
     * magnification that matches the video is the video's zoom */
    REQUIRE(quiver_image_view_get_texture(view) != nullptr);
    /* The picture the preview is drawn from is the frame's own size, so the size
     * the view is drawing it at is the frame scaled by the video's zoom: the
     * preview on screen is the size the video will be, and not the fit size it
     * was delivered at. */
    gint drawn_w = 0, drawn_h = 0;
    quiver_image_view_get_pixbuf_display_size_for_mode(view, viewer->GetVideoViewMode(),
        &drawn_w, &drawn_h);
    REQUIRE(drawn_w > 0);
    CHECK(drawn_w == Catch::Approx(list->GetCurrent().GetWidth() * zoom).margin(2.0));
    CHECK(drawn_h == Catch::Approx(list->GetCurrent().GetHeight() * zoom).margin(2.0));

    double cx = 0., cy = 0.;
    CHECK(quiver_image_view_get_view_center(view, &cx, &cy));
    CHECK(cx == Catch::Approx(pan_x).margin(0.05));
    CHECK(cy == Catch::Approx(pan_y).margin(0.05));

    viewer.reset();
}

TEST_CASE("Viewer keeps a deep zoom framed when a picture is redelivered sharper",
          "[unit][viewer][zoomkeep][imageview]")
{
    REQUIRE_DISPLAY();

    /* A deep zoom is where the two sizes of a picture stop being the same
     * thing: the picture is 600x600, but the texture drawn for it is the small
     * decode the loader starts from, and the geometry - the magnification, the
     * scroll range, what a kept center means - is in the picture.  A center
     * converted through the texture describes a smaller picture, lands off the
     * scroll range, gets clamped to the edge, and the clamped value is what the
     * next item inherits. */
    QuiverImageView *view = QUIVER_IMAGE_VIEW(quiver_image_view_new());
    REQUIRE(view != nullptr);
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), GTK_WIDGET(view));
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    GtkAdjustment *hadj = quiver_image_view_get_hadjustment(view);
    GtkAdjustment *vadj = quiver_image_view_get_vadjustment(view);
    REQUIRE(hadj != nullptr);
    REQUIRE(vadj != nullptr);

    /* the first picture at 600x600, drawn from a 60x60 texture */
    GdkTexture *small = make_flat_texture(60, 60, 200, 30, 30);
    REQUIRE(small != nullptr);
    quiver_image_view_set_texture_at_size_ex(view, small, 600, 600, TRUE);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    quiver_image_view_set_magnification(view, 9.0);
    while (g_main_context_iteration(NULL, FALSE));

    /* framed in the top right corner, the corner that is the hardest to hold */
    quiver_image_view_set_view_center(view, 0.9, 0.1);
    double cx = 0., cy = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &cx, &cy));
    REQUIRE(cx == Catch::Approx(0.9).margin(0.01));
    REQUIRE(cy == Catch::Approx(0.1).margin(0.01));

    /* the loader hands over the full resolution decode of the same picture, as
     * it does when it turns out the small one is too soft at this zoom */
    GdkTexture *full = make_flat_texture(600, 600, 30, 30, 200);
    REQUIRE(full != nullptr);
    quiver_image_view_set_texture_at_size_ex(view, full, 600, 600, FALSE);
    while (g_main_context_iteration(NULL, FALSE));

    /* the framing is the same point of the picture, not the same pixel of a
     * differently sized one, and neither a clamp nor a fit moved it */
    CHECK(quiver_image_view_get_view_center(view, &cx, &cy));
    CHECK(cx == Catch::Approx(0.9).margin(0.01));
    CHECK(cy == Catch::Approx(0.1).margin(0.01));
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(9.0));

    /* and a different picture, at a different size and aspect ratio, is framed
     * by the same fraction */
    GdkTexture *wide = make_flat_texture(96, 64, 30, 200, 30);
    REQUIRE(wide != nullptr);
    quiver_image_view_set_texture_at_size_ex(view, wide, 960, 640, FALSE);
    while (g_main_context_iteration(NULL, FALSE));
    CHECK(quiver_image_view_get_view_center(view, &cx, &cy));
    CHECK(cx == Catch::Approx(0.9).margin(0.01));
    CHECK(cy == Catch::Approx(0.1).margin(0.01));

    g_object_unref(small);
    g_object_unref(full);
    g_object_unref(wide);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
}

TEST_CASE("Viewer keeps \"keep zoom and pan\" selected while zooming a video",
          "[unit][viewer][zoomkeep][video]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 40) {
        for (int i = 0; i < rounds; ++i)
        {
            g_usleep(15000);
            while (g_main_context_iteration(NULL, FALSE));
        }
    };
    settle();

    /* a list of one file is a folder, so the video is looked up by name */
    const std::string video = QuiverTest_GetImagesDir() + "/sample_video.mp4";
    unsigned int video_index = list->GetSize();
    for (unsigned int i = 0; i < list->GetSize(); ++i)
        if (list->Get(i).GetFilePath() == video)
            video_index = i;
    REQUIRE(video_index < list->GetSize());
    REQUIRE(list->SetCurrentIndex(video_index));
    settle();
    REQUIRE(list->GetCurrent().IsVideo());
    gtk_widget_set_visible(viewer->GetWidget(), TRUE);
    settle();

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);

    GAction *zoom_keep = QuiverUtils::GetAction("ZoomKeep");
    GAction *zoom_in = QuiverUtils::GetAction("ZoomIn");
    GAction *zoom_out = QuiverUtils::GetAction("ZoomOut");
    REQUIRE(zoom_keep != nullptr);
    REQUIRE(zoom_in != nullptr);
    REQUIRE(zoom_out != nullptr);

    /* zooming a *quick preview* is the video's zoom applied to the thumbnail,
     * and it must not unselect the mode: the mode says what happens on the
     * next item, and the menu item is where the user sees what it says */
    viewer->StopVideo(true);
    settle();
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
    /* "keep zoom and pan" is picked the way a user picks it, and the radio is
     * set with it: the item is a toggle, so activating it with the shared radio
     * left checked by an earlier test would turn it off again */
    QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    g_action_activate(zoom_keep, NULL);
    settle();
    REQUIRE(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    REQUIRE(QuiverUtils::GetRadioActionCurrent("Zoom") == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    for (int i = 0; i < 3; ++i)
    {
        g_action_activate(zoom_in, NULL);
        settle(20);
        CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
        CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
        CHECK(QuiverUtils::GetRadioActionCurrent("Zoom") == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    }
    const double zoomed = viewer->GetVideoZoom();
    CHECK(zoomed > 1.0);

    /* and the same while it plays */
    g_action_activate(QuiverUtils::GetAction("VideoPlay"), NULL);
    settle(60);
    for (int i = 0; i < 2; ++i)
    {
        g_action_activate(zoom_in, NULL);
        settle(20);
        CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
        CHECK(QuiverUtils::GetRadioActionCurrent("Zoom") == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    }
    CHECK(viewer->GetVideoZoom() > zoomed);

    /* zooming back out to the fit level is still not a new mode either: that
     * mode is the one that has to survive the next item */
    viewer->SetVideoZoom(0.5);
    settle(20);
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(QuiverUtils::GetRadioActionCurrent("Zoom") == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    /* a zoom on a picture that is merely fitted does leave the fit modes,
     * which is what zooming a fitted picture means */
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);
    settle();
    g_action_activate(zoom_in, NULL);
    settle(20);
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM);

    viewer.reset();
}

TEST_CASE("Viewer keeps the view mode when it is given a different image list",
          "[unit][viewer][zoomkeep]")
{
	REQUIRE_DISPLAY();

	boost::shared_ptr<Viewer> viewer(new Viewer());
	viewer->RegisterActions();

	ImageListPtr list(new ImageList());
	std::list<std::string> files;
	files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
	files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
	list->Add(&files);
	viewer->SetImageList(list);

	GtkWidget *win = gtk_window_new();
	gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
	gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
	gtk_window_present(GTK_WINDOW(win));

	auto settle = [](int rounds = 200) {
		for (int i = 0; i < rounds; ++i)
		{
			while (g_main_context_iteration(NULL, FALSE));
			g_usleep(2000);
		}
	};
	settle();

	QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
	REQUIRE(view != nullptr);

	QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
	g_action_activate(QuiverUtils::GetAction("ZoomKeep"), NULL);
	settle(20);
	REQUIRE(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

	/* the app's own arrangement: the viewer browses a filtered view of the
	 * shared list, and switching folders repopulates that shared list - so the
	 * viewer is handed a *new* filter object over a list that changes under it */
	ImageListPtr shared(new ImageList());
	std::list<std::string> first_files;
	first_files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
	first_files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
	shared->Add(&first_files);
	IImageListViewPtr filtered(new ImageListFilter(
		IImageListViewPtr(shared), [](const QuiverFile &f) { return !f.IsFolder(); }));
	viewer->SetImageList(filtered);
	settle(300);
	REQUIRE(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

	std::list<std::string> other_files;
	other_files.push_back(QuiverTest_GetImagesDir() + "/sample.jpg");
	other_files.push_back(QuiverTest_GetImagesDir() + "/sample_rotated.jpg");
	shared->SetImageList(&other_files);
	settle(300);

	CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
	CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

	/* and the same when the new list opens on a video: its quick preview is
	 * delivered through the video path, which is a different route into the
	 * image view than a still's */
	std::list<std::string> video_files;
	video_files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
	video_files.push_back(QuiverTest_GetImagesDir() + "/sample.jpg");
	shared->SetImageList(&video_files);
	settle(300);

	CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
	CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

	viewer.reset();
}

TEST_CASE("Viewer zooms a video's quick preview toward the pointer",
          "[unit][viewer][video][zoomkeep]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 200) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);

    QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    g_action_activate(QuiverUtils::GetAction("ZoomKeep"), NULL);
    settle();

    int video_index = -1;
    for (guint i = 0; i < list->GetSize(); ++i)
        if (list->Get(i).IsVideo())
            video_index = (int)i;
    REQUIRE(video_index >= 0);
    REQUIRE(list->SetCurrentIndex((unsigned int)video_index));
    settle(300);

    /* the preview has to be on screen to be zoomed, and zoomed enough that the
     * picture is bigger than the viewport - otherwise there is no framing to
     * move and every anchor is the middle */
    REQUIRE(quiver_image_view_get_texture(view) != nullptr);
    viewer->SetVideoZoom(1.25);
    settle(20);
    viewer->SetVideoZoom(2.0);
    settle(20);
    REQUIRE(quiver_image_view_get_magnification(view) > 1.5);

    /* a pointer a quarter of the way in from the left, which is a different
     * anchor from the middle - and it has to actually be there, or the test
     * would be asserting the centred zoom it is meant to disprove */
    /* A pointer can only be asked for over a view that has been laid out, and
     * this one is not always: the viewer builds its own widget tree, and where
     * the test runs decides whether GTK realises it.  Saying so is the honest
     * outcome - a test that quietly asserted the centred zoom instead would pass
     * without ever having looked at a pointer. */
    int px = 0, py = 0;
    if (!quiver_test_pointer_over(GTK_WIDGET(view), 0.25, 0.5, &px, &py))
    {
        SKIP("the image view has no allocation, so the pointer cannot be placed over it");
    }
    settle(20);

    double before_x = 0., before_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &before_x, &before_y));
    const double zoom_before = viewer->GetVideoZoom();

    viewer->SetVideoZoom(zoom_before * 1.5);
    settle(20);

    double after_x = 0., after_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &after_x, &after_y));

    /* Zooming in about a point left of the middle moves the middle towards it:
     * the part of the picture that was under the pointer stays under it, so the
     * centre of the viewport is further left than it was.  A zoom about the
     * middle would leave the centre where it was, which is what this pins. */
    CHECK(after_x < before_x - 0.01);
    /* the pointer was level with the middle vertically, so that axis is the one
     * that does not move - otherwise the anchor is not where it was asked for */
    CHECK(fabs(after_y - before_y) < 0.02);

    /* and the video plays from where the zoom left the picture, not from where
     * the picture was centred before the zoom: a zoom to the cursor that the
     * video does not follow is a jump the moment play is pressed */
    CHECK(viewer->GetVideoPanFractionX() == Catch::Approx(after_x).margin(0.02));
    CHECK(viewer->GetVideoPanFractionY() == Catch::Approx(after_y).margin(0.02));

    viewer.reset();
}

TEST_CASE("Viewer keeps the zoom when switching between a still and a video",
          "[unit][viewer][zoomkeep][video]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 200) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);

    QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    g_action_activate(QuiverUtils::GetAction("ZoomKeep"), NULL);
    settle();

    int still_index = -1, video_index = -1;
    for (guint i = 0; i < list->GetSize(); ++i)
    {
        if (list->Get(i).IsVideo())
            video_index = (int)i;
        else if (still_index < 0)
            still_index = (int)i;
    }
    REQUIRE(still_index >= 0);
    REQUIRE(video_index >= 0);

    /* Zoom the still, which is where a session starts: a photo opened, zoomed
     * in, and then the next item happens to be a video. */
    REQUIRE(list->SetCurrentIndex((unsigned int)still_index));
    settle(300);
    REQUIRE(!list->GetCurrent().IsVideo());
    quiver_image_view_set_magnification(view, 2.0);
    settle(20);
    const double still_mag = quiver_image_view_get_magnification(view);
    REQUIRE(still_mag == Catch::Approx(2.0).margin(0.05));

    /* A magnification on a still and a zoom on a video are the same number for
     * the same framing: both are how much bigger than the picture the viewport
     * is showing it, and both pictures are declared at their own size - a video
     * at the frame's.  So arriving at the video has to leave it at that zoom:
     * the video's own remembered zoom is 1.0 (nobody zoomed the video), and
     * imposing that would drop the zoom the user just made and leave the
     * picture a size they never asked for. */
    REQUIRE(list->SetCurrentIndex((unsigned int)video_index));
    settle(300);
    REQUIRE(list->GetCurrent().IsVideo());
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(still_mag).margin(0.05));
    CHECK(viewer->GetVideoZoom() == Catch::Approx(still_mag).margin(0.05));

    /* and the way round: zoom the video, and the still after it comes up at the
     * same zoom rather than at whatever the stills were left at */
    viewer->SetVideoZoom(3.0);
    settle(20);
    const double video_zoom = viewer->GetVideoZoom();
    REQUIRE(video_zoom == Catch::Approx(3.0).margin(0.05));

    REQUIRE(list->SetCurrentIndex((unsigned int)still_index));
    settle(300);
    REQUIRE(!list->GetCurrent().IsVideo());
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(video_zoom).margin(0.05));

    viewer.reset();
}

TEST_CASE("Viewer keeps the pan when another item is chosen after a trip to the browser",
          "[unit][viewer][zoomkeep][browser]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 200) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();

    /* the viewer starts hidden, the way it does in the app until the browser
     * hands it an item - and an image view that is not on screen has no
     * allocation, so without this there is no pan to keep */
    viewer->Show();
    settle();

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);
    REQUIRE(gtk_widget_get_width(GTK_WIDGET(view)) > 1);

    QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    g_action_activate(QuiverUtils::GetAction("ZoomKeep"), NULL);
    settle();

    REQUIRE(list->SetCurrentIndex(0));
    settle(300);
    quiver_image_view_set_magnification(view, 2.0);
    settle(20);
    GtkAdjustment *hadj = quiver_image_view_get_hadjustment(view);
    GtkAdjustment *vadj = quiver_image_view_get_vadjustment(view);
    REQUIRE(hadj != nullptr);
    REQUIRE(vadj != nullptr);
    /* pan a known way into the scroll range, so the position is a fraction of
     * the picture rather than a count of pixels that means nothing here */
    const double h_room = gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
    const double v_room = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
    REQUIRE(h_room > 1.0);
    REQUIRE(v_room > 1.0);
    gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj) + h_room * 0.4);
    gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj) + v_room * 0.4);
    settle(20);
    double centre_x = 0., centre_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &centre_x, &centre_y));
    REQUIRE(std::abs(centre_x - 0.5) > 0.05);
    REQUIRE(std::abs(centre_y - 0.5) > 0.05);
    const double mag = quiver_image_view_get_magnification(view);

    /* Back to the browser: Escape no longer resets "keep zoom and pan", so it
     * leaves the viewer - and it says so, so that the caller goes on to the next
     * thing Escape does instead of quietly un-doing the mode. */
    CHECK(viewer->ResetViewMode() == false);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    viewer->Hide();
    settle(20);
    REQUIRE(gtk_widget_get_visible(viewer->GetWidget()) == false);

    /* and the same item chosen from the browser comes back in the framing it was
     * left in, in the mode the user chose */
    viewer->Show();
    settle(60);

    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(mag).margin(0.05));
    double new_x = 0., new_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &new_x, &new_y));
    INFO("centre before " << centre_x << "," << centre_y
         << " after " << new_x << "," << new_y);
    CHECK(new_x == Catch::Approx(centre_x).margin(0.02));
    CHECK(new_y == Catch::Approx(centre_y).margin(0.02));

    /* the same round trip with a different item chosen afterwards keeps it too */
    viewer->Hide();
    settle(20);
    REQUIRE(list->SetCurrentIndex(1));
    settle(400);
    viewer->Show();
    settle(60);

    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(mag).margin(0.05));
    REQUIRE(quiver_image_view_get_view_center(view, &new_x, &new_y));
    INFO("centre before " << centre_x << "," << centre_y
         << " after " << new_x << "," << new_y);
    CHECK(new_x == Catch::Approx(centre_x).margin(0.02));
    CHECK(new_y == Catch::Approx(centre_y).margin(0.02));

    viewer.reset();
}

TEST_CASE("Viewer keeps the zoom centre of the picture when the aspect ratio changes",
          "[unit][viewer][zoomkeep][video]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    /* 1920x1080 and 640x480.  The framing of a zoomed picture is kept as the
     * *centre* of the visible part, as a fraction of the frame, so the same
     * centre frames the same relative point of any frame: what is on screen
     * stays put when the aspect ratio changes.  An offset normalized against
     * each file's own scroll range would not, because that range is frame minus
     * viewport and so depends on the aspect ratio and the zoom - and the same
     * numbers would be describing two different points. */
    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video_4x3.mp4");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 40) {
        for (int i = 0; i < rounds; ++i)
        {
            g_usleep(15000);
            while (g_main_context_iteration(NULL, FALSE));
        }
    };
    settle();

    /* the list is sorted by name, so the files cannot be indexed by the order
     * they were added in */
    auto index_of = [&list](const std::string& name) {
        for (unsigned int i = 0; i < list->GetSize(); ++i)
            if (list->Get(i).GetFilePath() == name)
                return i;
        return list->GetSize();
    };
    const std::string wide = QuiverTest_GetImagesDir() + "/sample_video.mp4";
    const std::string other = QuiverTest_GetImagesDir() + "/sample_video_4x3.mp4";
    const std::string still = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(list->GetSize() == 3);
    REQUIRE(index_of(wide) != index_of(other));
    REQUIRE(index_of(wide) != index_of(still));
    REQUIRE(index_of(other) != index_of(still));

    REQUIRE(list->SetCurrentIndex(index_of(wide)));
    settle();
    REQUIRE(list->GetCurrent().IsVideo());
    gtk_widget_set_visible(viewer->GetWidget(), TRUE);
    settle();

    /* frame the quick preview: zoom it, then scroll it off the middle, which is
     * the only framing that tells the two representations apart (a centred view
     * survives both) */
    viewer->StopVideo(true);
    settle();
    GAction *zoom_in = QuiverUtils::GetAction("ZoomIn");
    REQUIRE(zoom_in != nullptr);
    for (int i = 0; i < 4; ++i)
    {
        g_action_activate(zoom_in, NULL);
        settle(30);
    }

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);
    GtkAdjustment *hadj = quiver_image_view_get_hadjustment(view);
    GtkAdjustment *vadj = quiver_image_view_get_vadjustment(view);
    REQUIRE(hadj != nullptr);
    REQUIRE(vadj != nullptr);
    const double h_upper = gtk_adjustment_get_upper(hadj);
    const double h_page = gtk_adjustment_get_page_size(hadj);
    REQUIRE(h_upper > h_page);
    gtk_adjustment_set_value(hadj, (h_upper - h_page) * 0.30);
    settle(30);

    /* where the centre of what the preview shows is, in fractions of the frame:
     * the image view's value is the left edge of the viewport inside the frame
     * scaled by the magnification, and its page is the size of that viewport, so
     * the centre of the picture being shown is (value + page/2) / upper */
    const double h_value = gtk_adjustment_get_value(hadj);
    const double v_upper = gtk_adjustment_get_upper(vadj);
    const double v_value = gtk_adjustment_get_value(vadj);
    const double expect_x = (h_value + h_page / 2.) / h_upper;
    const double expect_y = (v_value + gtk_adjustment_get_page_size(vadj) / 2.) / v_upper;
    REQUIRE(expect_x > 0.2);
    REQUIRE(expect_x < 0.8);

    /* playback starts there - the preview and the video are the same framing,
     * which is where the preview's own pan is read from */
    g_action_activate(QuiverUtils::GetAction("VideoPlay"), NULL);
    settle(60);
    CHECK(viewer->GetVideoPanFractionX() == Catch::Approx(expect_x).epsilon(0.01));
    CHECK(viewer->GetVideoPanFractionY() == Catch::Approx(expect_y).epsilon(0.01));

    /* and the framing survives a switch to a file with a different aspect ratio,
     * in the mode that is meant to keep it */
    QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    GAction *zoom_keep = QuiverUtils::GetAction("ZoomKeep");
    REQUIRE(zoom_keep != nullptr);
    g_action_activate(zoom_keep, NULL);
    settle();
    REQUIRE(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    REQUIRE(list->SetCurrentIndex(index_of(other)));
    settle(60);
    REQUIRE(list->GetCurrent().IsVideo());

    CHECK(viewer->GetVideoPanFractionX() == Catch::Approx(expect_x).epsilon(0.01));
    CHECK(viewer->GetVideoPanFractionY() == Catch::Approx(expect_y).epsilon(0.01));

    /* and on to a still: the mode is the viewer's, so the image around the
     * video is shown in it too, zoomed, instead of dropping back to fit as the
     * preview that borrowed the image view is handed back */
    REQUIRE(list->SetCurrentIndex(index_of(still)));
    settle(60);
    REQUIRE(!list->GetCurrent().IsVideo());
    CHECK(quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(viewer->GetImageView()))
              == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    GtkAdjustment* shadj = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(viewer->GetImageView()));
    REQUIRE(shadj != nullptr);
    CHECK(gtk_adjustment_get_page_size(shadj) < gtk_adjustment_get_upper(shadj));

    viewer.reset();
}

TEST_CASE("Viewer keeps a video's zoom and pan across items as a fraction of the frame",
          "[unit][viewer][zoomkeep][video]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_video.mp4");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
    gtk_window_present(GTK_WINDOW(win));

    auto settle = [](int rounds = 20) {
        for (int i = 0; i < rounds; ++i)
        {
            g_usleep(15000);
            while (g_main_context_iteration(NULL, FALSE));
        }
    };
    settle();

    int video_index = -1;
    int still_index = -1;
    for (guint i = 0; i < list->GetSize(); ++i)
    {
        if (list->Get(i).IsVideo())
            video_index = (int)i;
        else
            still_index = (int)i;
    }
    REQUIRE(video_index >= 0);
    REQUIRE(still_index >= 0);

    REQUIRE(list->SetCurrentIndex((unsigned int)video_index));
    REQUIRE(list->GetCurrent().IsVideo());
    settle();

    GtkWidget *playBtn = viewer->GetCenterPlayButton();
    REQUIRE(playBtn != nullptr);
    g_signal_emit_by_name(playBtn, "clicked");
    settle(40);

    /* zoom the playing video, so there is a zoom and a pan to keep */
    viewer->SetVideoZoom(3.0);
    for (int i = 0; i < 40 && viewer->GetVideoZoom() < 2.95; ++i)
    {
        g_usleep(35000);
        while (g_main_context_iteration(NULL, FALSE));
    }
    REQUIRE(viewer->GetVideoZoom() == Catch::Approx(3.0).margin(0.05));
    REQUIRE(viewer->CanVideoPan());
    const double kept_fraction_x = viewer->GetVideoPanFractionX();
    const double kept_fraction_y = viewer->GetVideoPanFractionY();
    CHECK(kept_fraction_x >= 0.0);
    CHECK(kept_fraction_x <= 1.0);
    CHECK(kept_fraction_y >= 0.0);
    CHECK(kept_fraction_y <= 1.0);

    /* back to "keep zoom and pan" - the mode that is meant to survive the next
     * item - and leave the video */
    QuiverUtils::SetRadioActionCurrent("Zoom", QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    GAction *zoom_keep = QuiverUtils::GetAction("ZoomKeep");
    REQUIRE(zoom_keep != nullptr);
    g_action_activate(zoom_keep, NULL);
    settle();
    REQUIRE(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);

    REQUIRE(list->SetCurrentIndex((unsigned int)still_index));
    settle(40);
    REQUIRE(list->GetCurrent().IsVideo() == false);

    /* and back to the video: in this mode the zoom and the pan are the ones it
     * was left at, the pan as a fraction of whatever frame is on screen */
    REQUIRE(list->SetCurrentIndex((unsigned int)video_index));
    settle(40);
    REQUIRE(list->GetCurrent().IsVideo());
    CHECK(viewer->GetVideoViewMode() == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(viewer->GetVideoZoom() == Catch::Approx(3.0).margin(0.05));
    CHECK(viewer->GetVideoPanFractionX() == Catch::Approx(kept_fraction_x).margin(0.02));
    CHECK(viewer->GetVideoPanFractionY() == Catch::Approx(kept_fraction_y).margin(0.02));

    viewer.reset();
}

TEST_CASE("Viewer keeps the zoom for an image delivered from the cache", "[unit][viewer][zoomkeep]")
{
    REQUIRE_DISPLAY();

    /* An image whose thumbnail is already cached is handed to the view
     * directly instead of going through the loader - the filmstrip keeps
     * thumbnails around, so switching to an image you have already seen takes
     * that route.  The rule the view cannot apply for itself is whether the
     * new image may reset it, so both routes ask Viewer for it; this is that
     * question, and a "keep zoom and pan" view must survive the answer. */
    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    GtkWidget *iv = quiver_image_view_new();
    gtk_window_set_child(GTK_WINDOW(win), iv);
    gtk_window_present(GTK_WINDOW(win));
    for (int i = 0; (i < 200) && (gtk_widget_get_width(iv) <= 1); ++i)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(5000);
    }
    REQUIRE(gtk_widget_get_width(iv) > 1);

    QuiverImageView *view = QUIVER_IMAGE_VIEW(iv);
    GdkTexture *first = make_flat_texture(400, 400, 200, 30, 30);
    GdkTexture *second = make_flat_texture(600, 800, 30, 30, 200);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    auto settle = [](int n) {
        for (int i = 0; i < n; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    quiver_image_view_set_texture_at_size_ex(view, first, 400, 400, TRUE);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    quiver_image_view_set_magnification(view, 2.0);
    const double kept_mag = quiver_image_view_get_magnification(view);
    REQUIRE(kept_mag == Catch::Approx(2.0));
    settle(20);

    /* the framing that has to survive the switch, set as a fraction of the
     * picture: 400x400 to a 600x800 portrait is both a different size and a
     * different aspect ratio, the case that used to come up centred, because a
     * scroll position in pixels of one picture is not a position in the next
     * one and off its edge the only thing left is the middle.  A fraction is
     * also the only unit a fraction of the viewport can even reach: the middle
     * of what is on screen can only be as far off the picture's own middle as
     * half the visible part. */
    quiver_image_view_set_view_center(view, 0.7, 0.7);
    double round_x = 0.0, round_y = 0.0;
    REQUIRE(quiver_image_view_get_view_center(view, &round_x, &round_y));
    REQUIRE(round_x == Catch::Approx(0.7).margin(0.01));
    REQUIRE(round_y == Catch::Approx(0.7).margin(0.01));

    /* pan off the middle through the adjustments, so a view that merely forgot
     * it was zoomed cannot pass by sitting at the default position */
    GtkAdjustment *hadj = quiver_image_view_get_hadjustment(view);
    GtkAdjustment *vadj = quiver_image_view_get_vadjustment(view);
    REQUIRE(hadj != nullptr);
    REQUIRE(vadj != nullptr);
    REQUIRE(gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj) > 1.0);
    gtk_adjustment_set_value(hadj, 100.0);
    gtk_adjustment_set_value(vadj, 60.0);
    double before_x = 0.0, before_y = 0.0;
    REQUIRE(quiver_image_view_get_view_center(view, &before_x, &before_y));

    /* a cached thumbnail arrives asking for the reset it would normally do */
    CHECK(Viewer::ShouldResetViewForNewImage(GTK_WIDGET(view), TRUE) == FALSE);
    quiver_image_view_set_texture_at_size_ex(view, second, 600, 800,
        Viewer::ShouldResetViewForNewImage(GTK_WIDGET(view), TRUE) ? TRUE : FALSE);
    settle(20);
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(kept_mag).margin(0.05));
    double after_x = 0.0, after_y = 0.0;
    REQUIRE(quiver_image_view_get_view_center(view, &after_x, &after_y));
    /* the same fraction of the picture - which is a different pixel of it, so
     * what was framed stays framed when the picture changes shape */
    CHECK(after_x == Catch::Approx(before_x).margin(0.01));
    CHECK(after_y == Catch::Approx(before_y).margin(0.01));
    CHECK(fabs(after_x * 600.0 - before_x * 400.0) > 1.0);
    CHECK(fabs(after_y * 800.0 - before_y * 400.0) > 1.0);

    /* the other modes are still reset, so the new image is re-fitted */
    for (QuiverImageViewMode mode : { QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW, QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE,
             QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH, QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN })
    {
        quiver_image_view_set_view_mode(view, mode);
        CHECK(Viewer::ShouldResetViewForNewImage(GTK_WIDGET(view), TRUE) == TRUE);
    }
    /* and with no view at all the caller still gets its own answer */
    CHECK(Viewer::ShouldResetViewForNewImage(nullptr, TRUE) == TRUE);
    CHECK(Viewer::ShouldResetViewForNewImage(nullptr, FALSE) == FALSE);

    g_object_unref(first);
    g_object_unref(second);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
}

TEST_CASE("Viewer keeps the zoom while switching between stills", "[unit][viewer][zoomkeep]")
{
    REQUIRE_DISPLAY();

    boost::shared_ptr<Viewer> viewer(new Viewer());
    viewer->RegisterActions();

    ImageListPtr list(new ImageList());
    std::list<std::string> files;
    files.push_back(QuiverTest_GetImagesDir() + "/sample_4k.jpg");
    files.push_back(QuiverTest_GetImagesDir() + "/sample_rotated.jpg");
    list->Add(&files);
    viewer->SetImageList(list);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), viewer->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_present(GTK_WINDOW(win));
    for (int i = 0; i < 200; ++i)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(2000);
    }

    QuiverImageView *view = QUIVER_IMAGE_VIEW(viewer->GetImageView());
    REQUIRE(view != nullptr);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    quiver_image_view_set_magnification(view, 2.0);
    const double kept_mag = quiver_image_view_get_magnification(view);
    REQUIRE(kept_mag == Catch::Approx(2.0));

    /* the list is sorted, so the still we are not on is the other one */
    const unsigned int current = list->GetCurrentIndex();
    const unsigned int other = (current == 0) ? 1 : 0;
    REQUIRE(list->SetCurrentIndex(other));
    for (int i = 0; i < 300; ++i)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(2000);
    }
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(kept_mag).margin(0.05));

    /* switching back keeps it too */
    REQUIRE(list->SetCurrentIndex(current));
    for (int i = 0; i < 300; ++i)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(2000);
    }
    CHECK(quiver_image_view_get_view_mode(view) == QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(kept_mag).margin(0.05));

    viewer.reset();
}

TEST_CASE("A view that is blanked between items keeps the framing of the next one",
          "[unit][viewer][zoomkeep][imageview]")
{
    REQUIRE_DISPLAY();

    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    QuiverImageView *view = QUIVER_IMAGE_VIEW(quiver_image_view_new());
    gtk_window_set_child(GTK_WINDOW(win), GTK_WIDGET(view));
    gtk_window_present(GTK_WINDOW(win));
    auto settle = [](int rounds = 200) {
        for (int i = 0; i < rounds; ++i)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(2000);
        }
    };
    settle();
    REQUIRE(gtk_widget_get_width(GTK_WIDGET(view)) > 1);

    GtkAdjustment *hadj = quiver_image_view_get_hadjustment(view);
    GtkAdjustment *vadj = quiver_image_view_get_vadjustment(view);
    REQUIRE(hadj != nullptr);
    REQUIRE(vadj != nullptr);

    GdkTexture *first = make_flat_texture(4000, 3000, 200, 30, 30);
    REQUIRE(first != nullptr);
    quiver_image_view_set_texture_at_size_ex(view, first, 4000, 3000, TRUE);
    quiver_image_view_set_view_mode(view, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    quiver_image_view_set_magnification(view, 2.0);
    settle(20);
    REQUIRE(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));
    REQUIRE(gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj) > 1.0);
    gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj) + 2000.0);
    gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj) + 1000.0);
    settle(20);
    double centre_x = 0., centre_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &centre_x, &centre_y));
    REQUIRE(centre_x > 0.05);
    REQUIRE(centre_y > 0.05);

    /* Choosing another item in the browser empties the view while the next
     * picture is loaded, and an empty view has no picture to read a centre
     * off - so the framing has to be held across the gap, or the next picture
     * comes up wherever the empty view left the scroll position: the top left
     * corner. */
    quiver_image_view_set_texture(view, NULL);
    settle(20);
    GdkTexture *second = make_flat_texture(3000, 4000, 30, 30, 200);
    REQUIRE(second != nullptr);
    quiver_image_view_set_texture_at_size_ex(view, second, 3000, 4000, FALSE);
    settle(20);

    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));
    double new_x = 0., new_y = 0.;
    REQUIRE(quiver_image_view_get_view_center(view, &new_x, &new_y));
    CHECK(new_x == Catch::Approx(centre_x).margin(0.02));
    CHECK(new_y == Catch::Approx(centre_y).margin(0.02));

    /* going back to the browser and picking something else keeps the same
     * framing again - the framing is the one the mode is holding, and the gap
     * between items is not a reason to let it go */
    GdkTexture *third = make_flat_texture(2000, 2000, 30, 200, 30);
    REQUIRE(third != nullptr);
    quiver_image_view_set_texture(view, NULL);
    settle(20);
    quiver_image_view_set_texture_at_size_ex(view, third, 2000, 2000, FALSE);
    settle(20);
    CHECK(quiver_image_view_get_magnification(view) == Catch::Approx(2.0));
    REQUIRE(quiver_image_view_get_view_center(view, &new_x, &new_y));
    CHECK(new_x == Catch::Approx(centre_x).margin(0.02));
    CHECK(new_y == Catch::Approx(centre_y).margin(0.02));

    /* A view with no viewport has no position to report, and so no position to
     * take as the one to keep: its empty adjustments would answer with the top
     * left corner, which is a position nobody chose.  (The browser has the
     * screen while the next item is chosen, so a view off screen is exactly a
     * view without a viewport.) */
    QuiverImageView *offscreen = QUIVER_IMAGE_VIEW(quiver_image_view_new());
    g_object_ref_sink(offscreen);
    REQUIRE(gtk_widget_get_width(GTK_WIDGET(offscreen)) <= 1);
    quiver_image_view_set_view_mode(offscreen, QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP);
    GdkTexture *flat = make_flat_texture(4000, 3000, 200, 30, 30);
    REQUIRE(flat != nullptr);
    quiver_image_view_set_texture_at_size_ex(offscreen, flat, 4000, 3000, TRUE);
    quiver_image_view_set_magnification(offscreen, 2.0);
    quiver_image_view_set_texture_at_size_ex(offscreen, third, 2000, 2000, FALSE);
    double no_x = -1., no_y = -1.;
    CHECK(quiver_image_view_get_view_center(offscreen, &no_x, &no_y) == false);
    g_object_unref(flat);
    g_object_unref(offscreen);

    g_object_unref(first);
    g_object_unref(second);
    g_object_unref(third);
    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
}
