#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define ADW_TYPE_DEMO_PAGE_INFO (adw_demo_page_info_get_type())

G_DECLARE_FINAL_TYPE (AdwDemoPageInfo, adw_demo_page_info, ADW, DEMO_PAGE_INFO, GObject)

#define ADW_TYPE_DEMO_PAGE_LIST (adw_demo_page_list_get_type())

G_DECLARE_FINAL_TYPE (AdwDemoPageList, adw_demo_page_list, ADW, DEMO_PAGE_LIST, GObject)

G_END_DECLS
