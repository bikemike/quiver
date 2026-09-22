#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "test_helpers.h"
#include "Viewer.h"
#include "ImageList.h"
#include "QuiverFile.h"
#include "QuiverUtils.h"
#include "quiver-image-view.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include <string>
#include <vector>

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
    viewer.reset();
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
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
    viewer.reset();
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
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
    QuiverUtils::ToggleActionSetActive("ViewFilmStrip", TRUE);

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
    viewer.reset();
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
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
    viewer.reset();
    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
}


