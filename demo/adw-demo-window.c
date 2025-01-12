#include "adw-demo-window.h"

#include <glib/gi18n.h>

#include "pages/about/adw-demo-page-about.h"
#include "pages/animations/adw-demo-page-animations.h"
#include "pages/avatar/adw-demo-page-avatar.h"
#include "pages/banners/adw-demo-page-banners.h"
#include "pages/buttons/adw-demo-page-buttons.h"
#include "pages/carousel/adw-demo-page-carousel.h"
#include "pages/clamp/adw-demo-page-clamp.h"
#include "pages/dialogs/adw-demo-page-dialogs.h"
#include "pages/flap/adw-demo-page-flap.h"
#include "pages/leaflet/adw-demo-page-leaflet.h"
#include "pages/lists/adw-demo-page-lists.h"
#include "pages/styles/adw-demo-page-styles.h"
#include "pages/tab-view/adw-demo-page-tab-view.h"
#include "pages/toasts/adw-demo-page-toasts.h"
#include "pages/view-switcher/adw-demo-page-view-switcher.h"
#include "pages/welcome/adw-demo-page-welcome.h"

struct _AdwDemoWindow
{
  AdwApplicationWindow parent_instance;

  GtkWidget *color_scheme_button;
  AdwLeaflet *main_leaflet;
  AdwLeaflet *subpage_leaflet;
  AdwDemoPageToasts *toasts_page;
};

G_DEFINE_FINAL_TYPE (AdwDemoWindow, adw_demo_window, ADW_TYPE_APPLICATION_WINDOW)

static char *
get_color_scheme_icon_name (gpointer user_data,
                            gboolean dark)
{
  return g_strdup (dark ? "light-mode-symbolic" : "dark-mode-symbolic");
}

static void
color_scheme_button_clicked_cb (AdwDemoWindow *self)
{
  AdwStyleManager *manager = adw_style_manager_get_default ();

  if (adw_style_manager_get_dark (manager))
    adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
  else
    adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_FORCE_DARK);
}

static void
notify_system_supports_color_schemes_cb (AdwDemoWindow *self)
{
  AdwStyleManager *manager = adw_style_manager_get_default ();
  gboolean supports = adw_style_manager_get_system_supports_color_schemes (manager);

  gtk_widget_set_visible (self->color_scheme_button, !supports);

  if (supports)
    adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_DEFAULT);
}

static void
notify_visible_child_cb (AdwDemoWindow *self)
{
  adw_leaflet_navigate (self->main_leaflet, ADW_NAVIGATION_DIRECTION_FORWARD);
}

static void
back_clicked_cb (AdwDemoWindow *self)
{
  adw_leaflet_navigate (self->main_leaflet, ADW_NAVIGATION_DIRECTION_BACK);
}

static void
leaflet_back_clicked_cb (AdwDemoWindow *self)
{
  adw_leaflet_navigate (self->subpage_leaflet, ADW_NAVIGATION_DIRECTION_BACK);
}

static void
leaflet_next_page_cb (AdwDemoWindow *self)
{
  adw_leaflet_navigate (self->subpage_leaflet, ADW_NAVIGATION_DIRECTION_FORWARD);
}

static void
toast_undo_cb (AdwDemoWindow *self)
{
  adw_demo_page_toasts_undo (self->toasts_page);
}

static void
adw_demo_window_class_init (AdwDemoWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_q, GDK_CONTROL_MASK, "window.close", NULL);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Adwaita1/Demo/ui/adw-demo-window.ui");
  gtk_widget_class_bind_template_child (widget_class, AdwDemoWindow, color_scheme_button);
  gtk_widget_class_bind_template_child (widget_class, AdwDemoWindow, main_leaflet);
  gtk_widget_class_bind_template_child (widget_class, AdwDemoWindow, subpage_leaflet);
  gtk_widget_class_bind_template_child (widget_class, AdwDemoWindow, toasts_page);
  gtk_widget_class_bind_template_callback (widget_class, get_color_scheme_icon_name);
  gtk_widget_class_bind_template_callback (widget_class, color_scheme_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, notify_visible_child_cb);
  gtk_widget_class_bind_template_callback (widget_class, back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, leaflet_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, leaflet_next_page_cb);

  gtk_widget_class_install_action (widget_class, "toast.undo", NULL, (GtkWidgetActionActivateFunc) toast_undo_cb);
}

static void
adw_demo_window_init (AdwDemoWindow *self)
{
  AdwStyleManager *manager = adw_style_manager_get_default ();

  g_type_ensure (ADW_TYPE_DEMO_PAGE_ABOUT);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_ANIMATIONS);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_BANNERS);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_AVATAR);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_BUTTONS);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_CAROUSEL);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_CLAMP);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_DIALOGS);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_FLAP);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_LEAFLET);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_LISTS);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_STYLES);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_TAB_VIEW);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_TOASTS);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_VIEW_SWITCHER);
  g_type_ensure (ADW_TYPE_DEMO_PAGE_WELCOME);

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (manager,
                           "notify::system-supports-color-schemes",
                           G_CALLBACK (notify_system_supports_color_schemes_cb),
                           self,
                           G_CONNECT_SWAPPED);

  notify_system_supports_color_schemes_cb (self);

  adw_leaflet_navigate (self->main_leaflet, ADW_NAVIGATION_DIRECTION_FORWARD);
}

AdwDemoWindow *
adw_demo_window_new (GtkApplication *application)
{
  return g_object_new (ADW_TYPE_DEMO_WINDOW, "application", application, NULL);
}
