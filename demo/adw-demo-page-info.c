#include "adw-demo-page-info.h"

#include <glib/gi18n.h>

#include "adw-demo-page.h"

struct _AdwDemoPageInfo
{
  GObject parent_instance;

  gchar *icon_name;
  gchar *title;
  GType gtype;
};

G_DEFINE_TYPE (AdwDemoPageInfo, adw_demo_page_info, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_ICON_NAME,
  PROP_TITLE,
  PROP_GTYPE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static inline void
set_string (gchar       **dest,
            const gchar  *source)
{
  if (*dest)
    g_free (*dest);

  *dest = g_strdup (source);
}

static void
adw_demo_page_info_finalize (GObject *object)
{
  AdwDemoPageInfo *self = ADW_DEMO_PAGE_INFO (object);

  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->icon_name, g_free);

  G_OBJECT_CLASS (adw_demo_page_info_parent_class)->finalize (object);
}

static void
adw_demo_page_info_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  AdwDemoPageInfo *self = ADW_DEMO_PAGE_INFO (object);

  switch (prop_id) {
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name);
    break;
  case PROP_TITLE:
    g_value_set_string (value, self->title);
    break;
  case PROP_GTYPE:
    g_value_set_gtype (value, self->gtype);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_demo_page_info_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  AdwDemoPageInfo *self = ADW_DEMO_PAGE_INFO (object);

  switch (prop_id) {
  case PROP_ICON_NAME:
    set_string (&self->icon_name, g_value_get_string (value));
    break;
  case PROP_TITLE:
    set_string (&self->title, g_value_get_string (value));
    break;
  case PROP_GTYPE:
    self->gtype = g_value_get_gtype (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_demo_page_info_class_init (AdwDemoPageInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = adw_demo_page_info_finalize;
  object_class->get_property = adw_demo_page_info_get_property;
  object_class->set_property = adw_demo_page_info_set_property;

  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name",
                         _("Icon Name"),
                         _("Icon Name"),
                         NULL,
                         G_PARAM_READWRITE);

  props[PROP_TITLE] =
    g_param_spec_string ("title",
                         _("Title"),
                         _("Title"),
                         NULL,
                         G_PARAM_READWRITE);

  props[PROP_GTYPE] =
    g_param_spec_gtype ("gtype",
                         _("Type"),
                         _("Type"),
                         ADW_TYPE_DEMO_PAGE,
                         G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
adw_demo_page_info_init (AdwDemoPageInfo *self)
{
}

struct _AdwDemoPageList
{
  GObject parent_instance;

  GPtrArray *data;
};

static void adw_demo_page_list_model_init (GListModelInterface *iface);
static void adw_demo_page_list_buildable_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (AdwDemoPageList, adw_demo_page_list, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, adw_demo_page_list_model_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, adw_demo_page_list_buildable_init))

static void
adw_demo_page_list_finalize (GObject *object)
{
  AdwDemoPageList *self = ADW_DEMO_PAGE_LIST (object);

  g_clear_pointer (&self->data, g_ptr_array_unref);

  G_OBJECT_CLASS (adw_demo_page_info_parent_class)->finalize (object);
}

static void
adw_demo_page_list_class_init (AdwDemoPageListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = adw_demo_page_list_finalize;
}

static void
adw_demo_page_list_init (AdwDemoPageList *self)
{
  self->data = g_ptr_array_new_full (0, g_object_unref);
}

static guint
adw_demo_page_list_model_get_n_items (GListModel *model)
{
  AdwDemoPageList *self = ADW_DEMO_PAGE_LIST (model);

  return (guint) self->data->len;
}

static gpointer
adw_demo_page_list_model_get_item (GListModel *model,
                                   guint       position)
{
  AdwDemoPageList *self = ADW_DEMO_PAGE_LIST (model);

  if (position >= self->data->len)
    return NULL;

  return g_object_ref (G_OBJECT (self->data->pdata[position]));
}

static GType
adw_demo_page_list_model_get_item_type (GListModel *model)
{
  return ADW_TYPE_DEMO_PAGE_INFO;
}

static void
adw_demo_page_list_model_init (GListModelInterface *iface)
{
  iface->get_n_items = adw_demo_page_list_model_get_n_items;
  iface->get_item = adw_demo_page_list_model_get_item;
  iface->get_item_type = adw_demo_page_list_model_get_item_type;
}

static void
adw_demo_page_list_buildable_add_child (GtkBuildable *buildable,
                                        GtkBuilder   *builder,
                                        GObject      *child,
                                        const char   *type)
{
  AdwDemoPageList *self = ADW_DEMO_PAGE_LIST (buildable);

  g_assert (ADW_IS_DEMO_PAGE_INFO (child));

  g_ptr_array_add (self->data, child);
}

static void
adw_demo_page_list_buildable_init (GtkBuildableIface *iface)
{
  iface->add_child = adw_demo_page_list_buildable_add_child;
}
