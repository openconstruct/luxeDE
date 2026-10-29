/* Headless ownership regression for repeated XML node creation. */
#include "../src/tree.h"
#include <glib/gstdio.h>

ObtXmlInst *xml_i;

int main(void)
{
    g_autofree char *directory = g_dir_make_tmp("obconf-xml-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_autofree char *path = g_build_filename(directory, "rc.xml", NULL);
    g_assert_true(g_file_set_contents(path, "<openbox_config/>\n", -1, NULL));
    xml_i = obt_xml_instance_new();
    g_assert_true(obt_xml_load_file(xml_i, path, "openbox_config"));
    g_assert_null(tree_find_node("missing/setting"));
    g_autofree gchar *fallback = tree_get_string("missing/setting", "fallback");
    g_assert_cmpstr(fallback, ==, "fallback");
    g_assert_cmpint(tree_get_int("missing/number", 7), ==, 7);
    g_assert_true(tree_get_bool("missing/enabled", TRUE));
    g_assert_null(tree_find_node("missing"));
    for(int i = 0; i < 2000; ++i) {
        xmlNodePtr node = tree_ensure_node("fixture:kind=value", "some text");
        g_assert_nonnull(node);
        xmlChar *property = xmlGetProp(node, BAD_CAST "kind");
        g_assert_cmpstr((const char *)property, ==, "value");
        xmlFree(property);
        xmlUnlinkNode(node);
        xmlFreeNode(node);
    }
    obt_xml_close(xml_i);
    obt_xml_instance_unref(xml_i);
    xmlCleanupParser();
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    return 0;
}
