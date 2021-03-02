/*
 * Copyright (C) 2019 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "adw-carousel.h"

#include "adw-animation-private.h"
#include "adw-navigation-direction.h"
#include "adw-swipe-tracker.h"
#include "adw-swipeable.h"

#include <math.h>

#define DEFAULT_DURATION 250

/**
 * SECTION:adw-carousel
 * @short_description: A paginated scrolling widget.
 * @title: AdwCarousel
 * @See_also: #AdwCarouselIndicatorDots, #AdwCarouselIndicatorLines
 *
 * The #AdwCarousel widget can be used to display a set of pages with
 * swipe-based navigation between them.
 *
 * # CSS nodes
 *
 * #AdwCarousel has a single CSS node with name carousel.
 *
 * Since: 1.0
 */

typedef struct {
  GtkWidget *widget;
  int position;
  gboolean visible;
  double size;
  double snap_point;
  gboolean adding;
  gboolean removing;

  gboolean shift_position;
  AdwAnimation *resize_animation;
} ChildInfo;

struct _AdwCarousel
{
  GtkWidget parent_instance;

  GList *children;
  double distance;
  double position;
  guint spacing;
  GtkOrientation orientation;
  guint animation_duration;
  guint reveal_duration;

  double animation_source_position;
  AdwAnimation *animation;
  ChildInfo *animation_target_child;

  AdwSwipeTracker *tracker;

  double position_shift;

  gulong scroll_timeout_id;
  gboolean can_scroll;
};

static void adw_carousel_buildable_init (GtkBuildableIface *iface);
static void adw_carousel_swipeable_init (AdwSwipeableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (AdwCarousel, adw_carousel, GTK_TYPE_WIDGET,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_ORIENTABLE, NULL)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, adw_carousel_buildable_init)
                         G_IMPLEMENT_INTERFACE (ADW_TYPE_SWIPEABLE, adw_carousel_swipeable_init))

static GtkBuildableIface *parent_buildable_iface;

enum {
  PROP_0,
  PROP_N_PAGES,
  PROP_POSITION,
  PROP_INTERACTIVE,
  PROP_SPACING,
  PROP_ANIMATION_DURATION,
  PROP_ALLOW_MOUSE_DRAG,
  PROP_ALLOW_LONG_SWIPES,
  PROP_REVEAL_DURATION,

  /* GtkOrientable */
  PROP_ORIENTATION,
  LAST_PROP = PROP_REVEAL_DURATION + 1,
};

static GParamSpec *props[LAST_PROP];

enum {
  SIGNAL_PAGE_CHANGED,
  SIGNAL_LAST_SIGNAL,
};
static guint signals[SIGNAL_LAST_SIGNAL];

static ChildInfo *
find_child_info (AdwCarousel *self,
                 GtkWidget   *widget)
{
  GList *l;

  for (l = self->children; l; l = l->next) {
    ChildInfo *info = l->data;

    if (widget == info->widget)
      return info;
  }

  return NULL;
}

static int
find_child_index (AdwCarousel *self,
                  GtkWidget   *widget,
                  gboolean     count_removing)
{
  GList *l;
  int i;

  i = 0;
  for (l = self->children; l; l = l->next) {
    ChildInfo *info = l->data;

    if (info->removing && !count_removing)
      continue;

    if (widget == info->widget)
      return i;

    i++;
  }

  return -1;
}

static GList *
get_nth_link (AdwCarousel *self,
              int          n)
{

  GList *l;
  int i;

  i = n;
  for (l = self->children; l; l = l->next) {
    ChildInfo *info = l->data;

    if (info->removing)
      continue;

    if (i-- == 0)
      return l;
  }

  return NULL;
}

static ChildInfo *
get_closest_child_at (AdwCarousel *self,
                      double       position,
                      gboolean     count_adding,
                      gboolean     count_removing)
{
  GList *l;
  ChildInfo *closest_child = NULL;

  for (l = self->children; l; l = l->next) {
    ChildInfo *child = l->data;

    if (child->adding && !count_adding)
      continue;

    if (child->removing && !count_removing)
      continue;

    if (!closest_child ||
        ABS (closest_child->snap_point - position) >
        ABS (child->snap_point - position))
      closest_child = child;
  }

  return closest_child;
}

static inline void
get_range (AdwCarousel *self,
           double      *lower,
           double      *upper)
{
  GList *l = g_list_last (self->children);
  ChildInfo *child = l ? l->data : NULL;

  if (lower)
    *lower = 0;

  if (upper)
    *upper = child ? child->snap_point : 0;
}

static GtkWidget *
get_page_at_position (AdwCarousel *self,
                      double       position)
{
  double lower = 0, upper = 0;
  ChildInfo *child;

  get_range (self, &lower, &upper);

  position = CLAMP (position, lower, upper);

  child = get_closest_child_at (self, position, TRUE, FALSE);

  if (!child)
    return NULL;

  return child->widget;
}

static void
update_shift_position_flag (AdwCarousel *self,
                            ChildInfo   *child)
{
  ChildInfo *closest_child;
  int animating_index, closest_index;

  /* We want to still shift position when the active child is being removed */
  closest_child = get_closest_child_at (self, self->position, FALSE, TRUE);

  if (!closest_child)
    return;

  animating_index = g_list_index (self->children, child);
  closest_index = g_list_index (self->children, closest_child);

  child->shift_position = (closest_index >= animating_index);
}

static void
set_position (AdwCarousel *self,
              double       position)
{
  GList *l;
  double lower = 0, upper = 0;

  get_range (self, &lower, &upper);

  position = CLAMP (position, lower, upper);

  self->position = position;
  gtk_widget_queue_allocate (GTK_WIDGET (self));

  for (l = self->children; l; l = l->next) {
    ChildInfo *child = l->data;

    if (child->adding || child->removing)
      update_shift_position_flag (self, child);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_POSITION]);
}

static void
resize_animation_value_cb (double     value,
                           ChildInfo *child)
{
  AdwCarousel *self = ADW_CAROUSEL (adw_animation_get_widget (child->resize_animation));
  double delta = value - child->size;

  child->size = value;

  if (child->shift_position)
    self->position_shift += delta;

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
resize_animation_done_cb (ChildInfo *child)
{
  AdwCarousel *self = ADW_CAROUSEL (adw_animation_get_widget (child->resize_animation));

  g_clear_pointer (&child->resize_animation, adw_animation_unref);

  if (child->adding)
    child->adding = FALSE;

  if (child->removing) {
    self->children = g_list_remove (self->children, child);

    g_free (child);
  }

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
animate_child_resize (AdwCarousel *self,
                      ChildInfo   *child,
                      double       value,
                      gint64       duration)
{
  double old_size = child->size;

  update_shift_position_flag (self, child);

  if (child->resize_animation)
    adw_animation_stop (child->resize_animation);

  child->resize_animation =
    adw_animation_new (GTK_WIDGET (self), old_size, value, duration,
                       adw_ease_out_cubic,
                       (AdwAnimationValueCallback) resize_animation_value_cb,
                       (AdwAnimationDoneCallback) resize_animation_done_cb,
                       child);

  adw_animation_start (child->resize_animation);
}

static void
shift_position (AdwCarousel *self,
                double       delta)
{
  set_position (self, self->position + delta);
  adw_swipe_tracker_shift_position (self->tracker, delta);
}

static void
scroll_animation_value_cb (double       value,
                           AdwCarousel *self)
{
  double position = adw_lerp (self->animation_source_position,
                              self->animation_target_child->snap_point,
                              value);

  set_position (self, position);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
scroll_animation_done_cb (AdwCarousel *self)
{
  GtkWidget *child;
  int index;

  g_clear_pointer (&self->animation, adw_animation_unref);
  self->animation_source_position = 0;
  self->animation_target_child = NULL;

  child = get_page_at_position (self, self->position);
  index = find_child_index (self, child, FALSE);

  g_signal_emit (self, signals[SIGNAL_PAGE_CHANGED], 0, index);
}

static void
scroll_to (AdwCarousel *self,
           GtkWidget   *widget,
           gint64       duration)
{
  self->animation_source_position = self->position;
  self->animation_target_child = find_child_info (self, widget);

  if (self->animation)
    adw_animation_stop (self->animation);

  self->animation =
    adw_animation_new (GTK_WIDGET (self), 0, 1, duration,
                       adw_ease_out_cubic,
                       (AdwAnimationValueCallback) scroll_animation_value_cb,
                       (AdwAnimationDoneCallback) scroll_animation_done_cb,
                       self);

  adw_animation_start (self->animation);
}

static inline double
get_closest_snap_point (AdwCarousel *self)
{
  ChildInfo *closest_child =
    get_closest_child_at (self, self->position, TRUE, TRUE);

  if (!closest_child)
    return 0;

  return closest_child->snap_point;
}

static void
begin_swipe_cb (AdwSwipeTracker        *tracker,
                AdwNavigationDirection  direction,
                gboolean                direct,
                AdwCarousel            *self)
{
  if (self->animation)
    adw_animation_stop (self->animation);
}

static void
update_swipe_cb (AdwSwipeTracker *tracker,
                 double           progress,
                 AdwCarousel     *self)
{
  set_position (self, progress);
}

static void
end_swipe_cb (AdwSwipeTracker *tracker,
              gint64           duration,
              double           to,
              AdwCarousel     *self)
{
  GtkWidget *child = get_page_at_position (self, to);

  scroll_to (self, child, duration);
}

static void
adw_carousel_switch_child (AdwSwipeable *swipeable,
                           guint         index,
                           gint64        duration)
{
  AdwCarousel *self = ADW_CAROUSEL (swipeable);
  GtkWidget *child = adw_carousel_get_nth_page (self, index);

  scroll_to (self, child, duration);
}

static AdwSwipeTracker *
adw_carousel_get_swipe_tracker (AdwSwipeable *swipeable)
{
  AdwCarousel *self = ADW_CAROUSEL (swipeable);

  return self->tracker;
}

static double
adw_carousel_get_distance (AdwSwipeable *swipeable)
{
  AdwCarousel *self = ADW_CAROUSEL (swipeable);

  return self->distance;
}

static double *
adw_carousel_get_snap_points (AdwSwipeable *swipeable,
                              int          *n_snap_points)
{
  AdwCarousel *self = ADW_CAROUSEL (swipeable);
  guint i, n_pages;
  double *points;
  GList *l;

  n_pages = MAX (g_list_length (self->children), 1);
  points = g_new0 (double, n_pages);

  i = 0;
  for (l = self->children; l; l = l->next) {
    ChildInfo *info = l->data;

    points[i++] = info->snap_point;
  }

  if (n_snap_points)
    *n_snap_points = n_pages;

  return points;
}

static double
adw_carousel_get_progress (AdwSwipeable *swipeable)
{
  AdwCarousel *self = ADW_CAROUSEL (swipeable);

  return adw_carousel_get_position (self);
}

static double
adw_carousel_get_cancel_progress (AdwSwipeable *swipeable)
{
  AdwCarousel *self = ADW_CAROUSEL (swipeable);

  return get_closest_snap_point (self);
}

/* Copied from GtkOrientable. Orientable widgets are supposed
 * to do this manually via a private GTK function. */
static void
set_orientable_style_classes (GtkOrientable *orientable)
{
  GtkOrientation orientation = gtk_orientable_get_orientation (orientable);
  GtkWidget *widget = GTK_WIDGET (orientable);

  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    gtk_widget_add_css_class (widget, "horizontal");
    gtk_widget_remove_css_class (widget, "vertical");
  } else {
    gtk_widget_add_css_class (widget, "vertical");
    gtk_widget_remove_css_class (widget, "horizontal");
  }
}

static void
update_orientation (AdwCarousel *self)
{
  gboolean reversed =
    self->orientation == GTK_ORIENTATION_HORIZONTAL &&
    gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->tracker),
                                  self->orientation);
  adw_swipe_tracker_set_reversed (self->tracker,
                                  reversed);

  set_orientable_style_classes (GTK_ORIENTABLE (self));
}

static gboolean
scroll_timeout_cb (AdwCarousel *self)
{
  self->can_scroll = TRUE;
  return G_SOURCE_REMOVE;
}

static gboolean
scroll_cb (AdwCarousel              *self,
           double                    dx,
           double                    dy,
           GtkEventControllerScroll *controller)
{
  GdkDevice *source_device;
  GdkInputSource input_source;
  int index;
  gboolean allow_vertical;
  GtkOrientation orientation;
  guint duration;
  GtkWidget *child;

  if (!self->can_scroll)
    return GDK_EVENT_PROPAGATE;

  if (!adw_carousel_get_interactive (self))
    return GDK_EVENT_PROPAGATE;

  source_device = gtk_event_controller_get_current_event_device (GTK_EVENT_CONTROLLER (controller));
  input_source = gdk_device_get_source (source_device);
  if (input_source == GDK_SOURCE_TOUCHPAD)
    return GDK_EVENT_PROPAGATE;

  /* Mice often don't have easily accessible horizontal scrolling,
   * hence allow vertical mouse scrolling regardless of orientation */
  allow_vertical = (input_source == GDK_SOURCE_MOUSE);

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
  index = 0;

  if (orientation == GTK_ORIENTATION_VERTICAL || allow_vertical) {
    if (dy > 0)
      index++;
    else if (dy < 0)
      index--;
  }

  if (orientation == GTK_ORIENTATION_HORIZONTAL && index == 0) {
    if (dx > 0)
      index++;
    else if (dx < 0)
      index--;
  }

  if (index == 0)
    return GDK_EVENT_PROPAGATE;

  child = get_page_at_position (self, self->position);

  index += find_child_index (self, child, FALSE);
  index = CLAMP (index, 0, (int) adw_carousel_get_n_pages (self) - 1);

  scroll_to (self, adw_carousel_get_nth_page (self, index), self->animation_duration);

  /* Don't allow the delay to go lower than 250ms */
  duration = MIN (self->animation_duration, DEFAULT_DURATION);

  self->can_scroll = FALSE;
  g_timeout_add (duration, (GSourceFunc) scroll_timeout_cb, self);

  return GDK_EVENT_STOP;
}

static void
adw_carousel_measure (GtkWidget      *widget,
                      GtkOrientation  orientation,
                      int             for_size,
                      int            *minimum,
                      int            *natural,
                      int            *minimum_baseline,
                      int            *natural_baseline)
{
  AdwCarousel *self = ADW_CAROUSEL (widget);
  GList *children;

  if (minimum)
    *minimum = 0;
  if (natural)
    *natural = 0;

  if (minimum_baseline)
    *minimum_baseline = -1;
  if (natural_baseline)
    *natural_baseline = -1;

  for (children = self->children; children; children = children->next) {
    ChildInfo *child_info = children->data;
    GtkWidget *child = child_info->widget;
    int child_min, child_nat;

    if (child_info->removing)
      continue;

    if (!gtk_widget_get_visible (child))
      continue;

    gtk_widget_measure (child, orientation, for_size,
                        &child_min, &child_nat, NULL, NULL);

    if (minimum)
      *minimum = MAX (*minimum, child_min);
    if (natural)
      *natural = MAX (*natural, child_nat);
  }
}

static void
adw_carousel_size_allocate (GtkWidget *widget,
                            int        width,
                            int        height,
                            int        baseline)
{
  AdwCarousel *self = ADW_CAROUSEL (widget);
  int size, child_width, child_height;
  GList *children;
  double x, y, offset;
  gboolean is_rtl;
  double snap_point;

  if (self->position_shift != 0) {
    shift_position (self, self->position_shift);
    self->position_shift = 0;
  }

  size = 0;
  for (children = self->children; children; children = children->next) {
    ChildInfo *child_info = children->data;
    GtkWidget *child = child_info->widget;
    int min, nat;
    int child_size;

    if (child_info->removing)
      continue;

    if (self->orientation == GTK_ORIENTATION_HORIZONTAL) {
      gtk_widget_measure (child, self->orientation,
                          height, &min, &nat, NULL, NULL);
      if (gtk_widget_get_hexpand (child))
        child_size = MAX (min, width);
      else
        child_size = MAX (min, nat);
    } else {
      gtk_widget_measure (child, self->orientation,
                          width, &min, &nat, NULL, NULL);
      if (gtk_widget_get_vexpand (child))
        child_size = MAX (min, height);
      else
        child_size = MAX (min, nat);
    }

    size = MAX (size, child_size);
  }

  self->distance = size + self->spacing;

  if (self->orientation == GTK_ORIENTATION_HORIZONTAL) {
    child_width = size;
    child_height = height;
  } else {
    child_width = width;
    child_height = size;
  }

  snap_point = 0;

  for (children = self->children; children; children = children->next) {
    ChildInfo *child_info = children->data;

    child_info->snap_point = snap_point + child_info->size - 1;

    snap_point += child_info->size;
  }

  if (!gtk_widget_get_realized (GTK_WIDGET (self)))
    return;

  x = 0;
  y = 0;

  is_rtl = (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL);

  if (self->orientation == GTK_ORIENTATION_VERTICAL)
    offset = (self->distance * self->position) - (height - child_height) / 2.0;
  else if (is_rtl)
    offset = -(self->distance * self->position) - (width - child_width) / 2.0;
  else
    offset = (self->distance * self->position) - (width - child_width) / 2.0;

  if (self->orientation == GTK_ORIENTATION_VERTICAL)
    y -= offset;
  else
    x -= offset;

  for (children = self->children; children; children = children->next) {
    ChildInfo *child_info = children->data;
    GskTransform *transform = gsk_transform_new ();

    if (!child_info->removing) {
      if (!gtk_widget_get_visible (child_info->widget))
        continue;

      if (self->orientation == GTK_ORIENTATION_VERTICAL) {
        child_info->position = y;
        child_info->visible = child_info->position < height &&
                              child_info->position + child_height > 0;

        transform = gsk_transform_translate (transform, &GRAPHENE_POINT_INIT (0, child_info->position));
      } else {
        child_info->position = x;
        child_info->visible = child_info->position < width &&
                              child_info->position + child_width > 0;

        transform = gsk_transform_translate (transform, &GRAPHENE_POINT_INIT (child_info->position, 0));
      }

      gtk_widget_allocate (child_info->widget, child_width, child_height, baseline, transform);
    }

    if (self->orientation == GTK_ORIENTATION_VERTICAL)
      y += self->distance * child_info->size;
    else if (is_rtl)
      x -= self->distance * child_info->size;
    else
      x += self->distance * child_info->size;
  }
}

static void
adw_carousel_direction_changed (GtkWidget        *widget,
                                GtkTextDirection  previous_direction)
{
  AdwCarousel *self = ADW_CAROUSEL (widget);

  update_orientation (self);
}

static void
adw_carousel_constructed (GObject *object)
{
  AdwCarousel *self = (AdwCarousel *)object;

  update_orientation (self);

  G_OBJECT_CLASS (adw_carousel_parent_class)->constructed (object);
}

static void
adw_carousel_dispose (GObject *object)
{
  AdwCarousel *self = ADW_CAROUSEL (object);

  g_clear_object (&self->tracker);

  if (self->scroll_timeout_id != 0) {
    g_source_remove (self->scroll_timeout_id);
    self->scroll_timeout_id = 0;
  }

  G_OBJECT_CLASS (adw_carousel_parent_class)->dispose (object);
}

static void
adw_carousel_finalize (GObject *object)
{
  AdwCarousel *self = ADW_CAROUSEL (object);

  g_list_free_full (self->children, (GDestroyNotify) g_free);

  G_OBJECT_CLASS (adw_carousel_parent_class)->finalize (object);
}

static void
adw_carousel_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  AdwCarousel *self = ADW_CAROUSEL (object);

  switch (prop_id) {
  case PROP_N_PAGES:
    g_value_set_uint (value, adw_carousel_get_n_pages (self));
    break;

  case PROP_POSITION:
    g_value_set_double (value, adw_carousel_get_position (self));
    break;

  case PROP_INTERACTIVE:
    g_value_set_boolean (value, adw_carousel_get_interactive (self));
    break;

  case PROP_SPACING:
    g_value_set_uint (value, adw_carousel_get_spacing (self));
    break;

  case PROP_ALLOW_MOUSE_DRAG:
    g_value_set_boolean (value, adw_carousel_get_allow_mouse_drag (self));
    break;

  case PROP_ALLOW_LONG_SWIPES:
    g_value_set_boolean (value, adw_carousel_get_allow_long_swipes (self));
    break;

  case PROP_REVEAL_DURATION:
    g_value_set_uint (value, adw_carousel_get_reveal_duration (self));
    break;

  case PROP_ORIENTATION:
    g_value_set_enum (value, self->orientation);
    break;

  case PROP_ANIMATION_DURATION:
    g_value_set_uint (value, adw_carousel_get_animation_duration (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_carousel_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  AdwCarousel *self = ADW_CAROUSEL (object);

  switch (prop_id) {
  case PROP_INTERACTIVE:
    adw_carousel_set_interactive (self, g_value_get_boolean (value));
    break;

  case PROP_SPACING:
    adw_carousel_set_spacing (self, g_value_get_uint (value));
    break;

  case PROP_ANIMATION_DURATION:
    adw_carousel_set_animation_duration (self, g_value_get_uint (value));
    break;

  case PROP_REVEAL_DURATION:
    adw_carousel_set_reveal_duration (self, g_value_get_uint (value));
    break;

  case PROP_ALLOW_MOUSE_DRAG:
    adw_carousel_set_allow_mouse_drag (self, g_value_get_boolean (value));
    break;

  case PROP_ALLOW_LONG_SWIPES:
    adw_carousel_set_allow_long_swipes (self, g_value_get_boolean (value));
    break;

  case PROP_ORIENTATION:
    {
      GtkOrientation orientation = g_value_get_enum (value);
      if (orientation != self->orientation) {
        self->orientation = orientation;
        update_orientation (self);
        gtk_widget_queue_resize (GTK_WIDGET (self));
        g_object_notify (G_OBJECT (self), "orientation");
      }
    }
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_carousel_swipeable_init (AdwSwipeableInterface *iface)
{
  iface->switch_child = adw_carousel_switch_child;
  iface->get_swipe_tracker = adw_carousel_get_swipe_tracker;
  iface->get_distance = adw_carousel_get_distance;
  iface->get_snap_points = adw_carousel_get_snap_points;
  iface->get_progress = adw_carousel_get_progress;
  iface->get_cancel_progress = adw_carousel_get_cancel_progress;
}

static void
adw_carousel_class_init (AdwCarouselClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = adw_carousel_constructed;
  object_class->dispose = adw_carousel_dispose;
  object_class->finalize = adw_carousel_finalize;
  object_class->get_property = adw_carousel_get_property;
  object_class->set_property = adw_carousel_set_property;

  widget_class->measure = adw_carousel_measure;
  widget_class->size_allocate = adw_carousel_size_allocate;
  widget_class->direction_changed = adw_carousel_direction_changed;

  /**
   * AdwCarousel:n-pages:
   *
   * The number of pages in a #AdwCarousel
   *
   * Since: 1.0
   */
  props[PROP_N_PAGES] =
    g_param_spec_uint ("n-pages",
                       _("Number of pages"),
                       _("Number of pages"),
                       0,
                       G_MAXUINT,
                       0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwCarousel:position:
   *
   * Current scrolling position, unitless. 1 matches 1 page. Use
   * adw_carousel_scroll_to() for changing it.
   *
   * Since: 1.0
   */
  props[PROP_POSITION] =
    g_param_spec_double ("position",
                         _("Position"),
                         _("Current scrolling position"),
                         0,
                         G_MAXDOUBLE,
                         0,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwCarousel:interactive:
   *
   * Whether the carousel can be navigated. This can be used to temporarily
   * disable a #AdwCarousel to only allow navigating it in a certain state.
   *
   * Since: 1.0
   */
  props[PROP_INTERACTIVE] =
    g_param_spec_boolean ("interactive",
                          _("Interactive"),
                          _("Whether the widget can be swiped"),
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwCarousel:spacing:
   *
   * Spacing between pages in pixels.
   *
   * Since: 1.0
   */
  props[PROP_SPACING] =
    g_param_spec_uint ("spacing",
                       _("Spacing"),
                       _("Spacing between pages"),
                       0,
                       G_MAXUINT,
                       0,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwCarousel:animation-duration:
   *
   * Animation duration in milliseconds, used by adw_carousel_scroll_to().
   *
   * Since: 1.0
   */
  props[PROP_ANIMATION_DURATION] =
    g_param_spec_uint ("animation-duration",
                       _("Animation duration"),
                       _("Default animation duration"),
                       0, G_MAXUINT, DEFAULT_DURATION,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwCarousel:allow-mouse-drag:
   *
   * Sets whether the #AdwCarousel can be dragged with mouse pointer. If the
   * value is %FALSE, dragging is only available on touch.
   *
   * Since: 1.0
   */
  props[PROP_ALLOW_MOUSE_DRAG] =
    g_param_spec_boolean ("allow-mouse-drag",
                          _("Allow mouse drag"),
                          _("Whether to allow dragging with mouse pointer"),
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwCarousel:allow-long-swipes:
   *
   * Whether to allow swiping for more than one page at a time. If the value is
   * %FALSE, each swipe can only move to the adjacent pages.
   *
   * Since: 1.0
   */
  props[PROP_ALLOW_LONG_SWIPES] =
    g_param_spec_boolean ("allow-long-swipes",
                          _("Allow long swipes"),
                          _("Whether to allow swiping for more than one page at a time"),
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwCarousel:reveal-duration:
   *
   * Page reveal duration in milliseconds.
   *
   * Since: 1.0
   */
  props[PROP_REVEAL_DURATION] =
    g_param_spec_uint ("reveal-duration",
                       _("Reveal duration"),
                       _("Page reveal duration"),
                       0,
                       G_MAXUINT,
                       0,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_override_property (object_class,
                                    PROP_ORIENTATION,
                                    "orientation");

  g_object_class_install_properties (object_class, LAST_PROP, props);

  /**
   * AdwCarousel::page-changed:
   * @self: The #AdwCarousel instance
   * @index: Current page
   *
   * This signal is emitted after a page has been changed. This can be used to
   * implement "infinite scrolling" by connecting to this signal and amending
   * the pages.
   *
   * Since: 1.0
   */
  signals[SIGNAL_PAGE_CHANGED] =
    g_signal_new ("page-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_UINT);

  gtk_widget_class_set_css_name (widget_class, "carousel");
}

static void
adw_carousel_init (AdwCarousel *self)
{
  GtkEventController *controller;

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);

  self->orientation = GTK_ORIENTATION_HORIZONTAL;
  self->reveal_duration = 0;
  self->animation_duration = DEFAULT_DURATION;
  self->can_scroll = TRUE;

  self->tracker = adw_swipe_tracker_new (ADW_SWIPEABLE (self));
  adw_swipe_tracker_set_allow_mouse_drag (self->tracker, TRUE);

  g_signal_connect_object (self->tracker, "begin-swipe", G_CALLBACK (begin_swipe_cb), self, 0);
  g_signal_connect_object (self->tracker, "update-swipe", G_CALLBACK (update_swipe_cb), self, 0);
  g_signal_connect_object (self->tracker, "end-swipe", G_CALLBACK (end_swipe_cb), self, 0);

  controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect_swapped (controller, "scroll", G_CALLBACK (scroll_cb), self);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);
}

static void
adw_carousel_buildable_add_child (GtkBuildable *buildable,
                                  GtkBuilder   *builder,
                                  GObject      *child,
                                  const char   *type)
{
  if (GTK_IS_WIDGET (child))
    adw_carousel_append (ADW_CAROUSEL (buildable), GTK_WIDGET (child));
  else
    parent_buildable_iface->add_child (buildable, builder, child, type);
}

static void
adw_carousel_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);

  iface->add_child = adw_carousel_buildable_add_child;
}

/**
 * adw_carousel_new:
 *
 * Create a new #AdwCarousel widget.
 *
 * Returns: The newly created #AdwCarousel widget
 *
 * Since: 1.0
 */
GtkWidget *
adw_carousel_new (void)
{
  return g_object_new (ADW_TYPE_CAROUSEL, NULL);
}

/**
 * adw_carousel_prepend:
 * @self: a #AdwCarousel
 * @child: a widget to add
 *
 * Prepends @child to @self
 *
 * Since: 1.0
 */
void
adw_carousel_prepend (AdwCarousel *self,
                      GtkWidget   *widget)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  adw_carousel_insert (self, widget, 0);
}

/**
 * adw_carousel_append:
 * @self: a #AdwCarousel
 * @child: a widget to add
 *
 * Appends @child to @self
 *
 * Since: 1.0
 */
void
adw_carousel_append (AdwCarousel *self,
                     GtkWidget   *widget)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  adw_carousel_insert (self, widget, -1);
}

/**
 * adw_carousel_insert:
 * @self: a #AdwCarousel
 * @child: a widget to add
 * @position: the position to insert @child in.
 *
 * Inserts @child into @self at position @position.
 *
 * If position is -1, or larger than the number of pages,
 * @child will be appended to the end.
 *
 * Since: 1.0
 */
void
adw_carousel_insert (AdwCarousel *self,
                     GtkWidget   *widget,
                     int          position)
{
  ChildInfo *info;
  GList *prev_link;

  g_return_if_fail (ADW_IS_CAROUSEL (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  info = g_new0 (ChildInfo, 1);
  info->widget = widget;
  info->size = 0;
  info->adding = TRUE;

  if (position >= 0)
    prev_link = get_nth_link (self, position);
  else
    prev_link = NULL;

  self->children = g_list_insert_before (self->children, prev_link, info);

  gtk_widget_set_parent (widget, GTK_WIDGET (self));

  gtk_widget_queue_allocate (GTK_WIDGET (self));

  animate_child_resize (self, info, 1, self->reveal_duration);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_PAGES]);
}
/**
 * adw_carousel_reorder:
 * @self: a #AdwCarousel
 * @child: a widget to add
 * @position: the position to move @child to.
 *
 * Moves @child into position @position.
 *
 * If position is -1, or larger than the number of pages, @child will be moved
 * to the end.
 *
 * Since: 1.0
 */
void
adw_carousel_reorder (AdwCarousel *self,
                      GtkWidget   *child,
                      int          position)
{
  ChildInfo *info, *prev_info;
  GList *link, *prev_link;
  int old_position;
  double closest_point, old_point, new_point;

  g_return_if_fail (ADW_IS_CAROUSEL (self));
  g_return_if_fail (GTK_IS_WIDGET (child));
  closest_point = get_closest_snap_point (self);

  info = find_child_info (self, child);
  link = g_list_find (self->children, info);
  old_position = g_list_position (self->children, link);

  if (position == old_position)
    return;

  old_point = ((ChildInfo *) link->data)->snap_point;

  if (position < 0 || position >= adw_carousel_get_n_pages (self))
    prev_link = g_list_last (self->children);
  else
    prev_link = get_nth_link (self, position);

  prev_info = prev_link->data;
  new_point = prev_info->snap_point;
  if (new_point > old_point)
    new_point -= prev_info->size;

  self->children = g_list_remove_link (self->children, link);
  self->children = g_list_insert_before (self->children, prev_link, link->data);

  if (closest_point == old_point)
    shift_position (self, new_point - old_point);
  else if (old_point > closest_point && closest_point >= new_point)
    shift_position (self, info->size);
  else if (new_point >= closest_point && closest_point > old_point)
    shift_position (self, -info->size);
}

/**
 * adw_carousel_remove:
 * @self: a #AdwCarousel
 * @child: a widget to remove
 *
 * Removes @child from @self
 *
 * Since: 1.0
 */
void
adw_carousel_remove (AdwCarousel *self,
                     GtkWidget   *child)
{
  ChildInfo *info;

  g_return_if_fail (ADW_IS_CAROUSEL (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  info = find_child_info (self, child);

  if (!info)
    return;

  info->removing = TRUE;

  gtk_widget_unparent (child);

  info->widget = NULL;

  if (!gtk_widget_in_destruction (GTK_WIDGET (self)))
    animate_child_resize (self, info, 0, self->reveal_duration);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_PAGES]);
}

/**
 * adw_carousel_scroll_to:
 * @self: a #AdwCarousel
 * @widget: a child of @self
 *
 * Scrolls to @widget position with an animation.
 * #AdwCarousel:animation-duration property can be used for controlling the
 * duration.
 *
 * Since: 1.0
 */
void
adw_carousel_scroll_to (AdwCarousel *self,
                        GtkWidget   *widget)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  adw_carousel_scroll_to_full (self, widget, self->animation_duration);
}

/**
 * adw_carousel_scroll_to_full:
 * @self: a #AdwCarousel
 * @widget: a child of @self
 * @duration: animation duration in milliseconds
 *
 * Scrolls to @widget position with an animation.
 *
 * Since: 1.0
 */
void
adw_carousel_scroll_to_full (AdwCarousel *self,
                             GtkWidget   *widget,
                             gint64       duration)
{
  int index;

  g_return_if_fail (ADW_IS_CAROUSEL (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));
  g_return_if_fail (duration >= 0);

  index = find_child_index (self, widget, FALSE);
  scroll_to (self, widget, duration);
  adw_swipeable_emit_child_switched (ADW_SWIPEABLE (self), index, duration);
}

/**
 * adw_carousel_get_nth_page:
 * @self: a #AdwCarousel
 * @n: index of the page
 *
 * Gets the page at position @n.
 *
 * Returns: (transfer none): the page
 *
 * Since: 1.0
 */
GtkWidget *
adw_carousel_get_nth_page (AdwCarousel *self,
                           guint        n)
{
  ChildInfo *info;

  g_return_val_if_fail (ADW_IS_CAROUSEL (self), NULL);
  g_return_val_if_fail (n < adw_carousel_get_n_pages (self), NULL);

  info = get_nth_link (self, n)->data;

  return info->widget;
}

/**
 * adw_carousel_get_n_pages:
 * @self: a #AdwCarousel
 *
 * Gets the number of pages in @self.
 *
 * Returns: The number of pages in @self
 *
 * Since: 1.0
 */
guint
adw_carousel_get_n_pages (AdwCarousel *self)
{
  GList *l;
  guint n_pages;

  g_return_val_if_fail (ADW_IS_CAROUSEL (self), 0);

  n_pages = 0;
  for (l = self->children; l; l = l->next) {
    ChildInfo *child = l->data;

    if (!child->removing)
      n_pages++;
  }

  return n_pages;
}

/**
 * adw_carousel_get_position:
 * @self: a #AdwCarousel
 *
 * Gets current scroll position in @self. It's unitless, 1 matches 1 page.
 *
 * Returns: The scroll position
 *
 * Since: 1.0
 */
double
adw_carousel_get_position (AdwCarousel *self)
{
  g_return_val_if_fail (ADW_IS_CAROUSEL (self), 0.0);

  return self->position;
}

/**
 * adw_carousel_get_interactive
 * @self: a #AdwCarousel
 *
 * Gets whether @self can be navigated.
 *
 * Returns: %TRUE if @self can be swiped
 *
 * Since: 1.0
 */
gboolean
adw_carousel_get_interactive (AdwCarousel *self)
{
  g_return_val_if_fail (ADW_IS_CAROUSEL (self), FALSE);

  return adw_swipe_tracker_get_enabled (self->tracker);
}

/**
 * adw_carousel_set_interactive
 * @self: a #AdwCarousel
 * @interactive: whether @self can be swiped.
 *
 * Sets whether @self can be navigated. This can be used to temporarily disable
 * a #AdwCarousel to only allow swiping in a certain state.
 *
 * Since: 1.0
 */
void
adw_carousel_set_interactive (AdwCarousel *self,
                              gboolean     interactive)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));

  interactive = !!interactive;

  if (adw_swipe_tracker_get_enabled (self->tracker) == interactive)
    return;

  adw_swipe_tracker_set_enabled (self->tracker, interactive);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INTERACTIVE]);
}

/**
 * adw_carousel_get_spacing:
 * @self: a #AdwCarousel
 *
 * Gets spacing between pages in pixels.
 *
 * Returns: Spacing between pages
 *
 * Since: 1.0
 */
guint
adw_carousel_get_spacing (AdwCarousel *self)
{
  g_return_val_if_fail (ADW_IS_CAROUSEL (self), 0);

  return self->spacing;
}

/**
 * adw_carousel_set_spacing:
 * @self: a #AdwCarousel
 * @spacing: the new spacing value
 *
 * Sets spacing between pages in pixels.
 *
 * Since: 1.0
 */
void
adw_carousel_set_spacing (AdwCarousel *self,
                          guint        spacing)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));

  if (self->spacing == spacing)
    return;

  self->spacing = spacing;
  gtk_widget_queue_resize (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SPACING]);
}

/**
 * adw_carousel_get_animation_duration:
 * @self: a #AdwCarousel
 *
 * Gets animation duration used by adw_carousel_scroll_to().
 *
 * Returns: Animation duration in milliseconds
 *
 * Since: 1.0
 */
guint
adw_carousel_get_animation_duration (AdwCarousel *self)
{
  g_return_val_if_fail (ADW_IS_CAROUSEL (self), 0);

  return self->animation_duration;
}

/**
 * adw_carousel_set_animation_duration:
 * @self: a #AdwCarousel
 * @duration: animation duration in milliseconds
 *
 * Sets animation duration used by adw_carousel_scroll_to().
 *
 * Since: 1.0
 */
void
adw_carousel_set_animation_duration (AdwCarousel *self,
                                     guint        duration)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));

  if (self->animation_duration == duration)
    return;

  self->animation_duration = duration;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ANIMATION_DURATION]);
}

/**
 * adw_carousel_get_allow_mouse_drag:
 * @self: a #AdwCarousel
 *
 * Sets whether @self can be dragged with mouse pointer
 *
 * Returns: %TRUE if @self can be dragged with mouse
 *
 * Since: 1.0
 */
gboolean
adw_carousel_get_allow_mouse_drag (AdwCarousel *self)
{
  g_return_val_if_fail (ADW_IS_CAROUSEL (self), FALSE);

  return adw_swipe_tracker_get_allow_mouse_drag (self->tracker);
}

/**
 * adw_carousel_set_allow_mouse_drag:
 * @self: a #AdwCarousel
 * @allow_mouse_drag: whether @self can be dragged with mouse pointer
 *
 * Sets whether @self can be dragged with mouse pointer. If @allow_mouse_drag
 * is %FALSE, dragging is only available on touch.
 *
 * Since: 1.0
 */
void
adw_carousel_set_allow_mouse_drag (AdwCarousel *self,
                                   gboolean     allow_mouse_drag)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));

  allow_mouse_drag = !!allow_mouse_drag;

  if (adw_carousel_get_allow_mouse_drag (self) == allow_mouse_drag)
    return;

  adw_swipe_tracker_set_allow_mouse_drag (self->tracker, allow_mouse_drag);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_MOUSE_DRAG]);
}

/**
 * adw_carousel_get_allow_long_swipes:
 * @self: a #AdwCarousel
 *
 * Whether to allow swiping for more than one page at a time. If the value is
 * %FALSE, each swipe can only move to the adjacent pages.
 *
 * Returns: %TRUE if long swipes are allowed, %FALSE otherwise
 *
 * Since: 1.0
 */
gboolean
adw_carousel_get_allow_long_swipes (AdwCarousel *self)
{
  g_return_val_if_fail (ADW_IS_CAROUSEL (self), FALSE);

  return adw_swipe_tracker_get_allow_long_swipes (self->tracker);
}

/**
 * adw_carousel_set_allow_long_swipes:
 * @self: a #AdwCarousel
 * @allow_long_swipes: whether to allow long swipes
 *
 * Sets whether to allow swiping for more than one page at a time. If the value
 * is %FALSE, each swipe can only move to the adjacent pages.
 *
 * Since: 1.0
 */
void
adw_carousel_set_allow_long_swipes (AdwCarousel *self,
                                    gboolean     allow_long_swipes)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));

  allow_long_swipes = !!allow_long_swipes;

  if (adw_swipe_tracker_get_allow_long_swipes (self->tracker) == allow_long_swipes)
    return;

  adw_swipe_tracker_set_allow_long_swipes (self->tracker, allow_long_swipes);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_LONG_SWIPES]);
}

/**
 * adw_carousel_get_reveal_duration:
 * @self: a #AdwCarousel
 *
 * Gets duration of the animation used when adding or removing pages in
 * milliseconds.
 *
 * Returns: Page reveal duration
 *
 * Since: 1.0
 */
guint
adw_carousel_get_reveal_duration (AdwCarousel *self)
{
  g_return_val_if_fail (ADW_IS_CAROUSEL (self), 0);

  return self->reveal_duration;
}

/**
 * adw_carousel_set_reveal_duration:
 * @self: a #AdwCarousel
 * @reveal_duration: the new reveal duration value
 *
 * Sets duration of the animation used when adding or removing pages in
 * milliseconds.
 *
 * Since: 1.0
 */
void
adw_carousel_set_reveal_duration (AdwCarousel *self,
                                  guint        reveal_duration)
{
  g_return_if_fail (ADW_IS_CAROUSEL (self));

  if (self->reveal_duration == reveal_duration)
    return;

  self->reveal_duration = reveal_duration;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_REVEAL_DURATION]);
}
