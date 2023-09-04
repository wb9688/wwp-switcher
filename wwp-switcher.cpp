#include <cstring>
#include <iostream>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <giomm/desktopappinfo.h>
#include <giomm/file.h>
#include <giomm/init.h>
#include <pango/pangocairo.h>
#include <wayfire/plugin.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

/*
 * This plugin provides abilities to switch between views.
 * It works similarly to the alt-tab binding in Windows or GNOME
 */

class switcher_texture
{
  public:
    wf::geometry_t rect;
    std::unique_ptr<wf::simple_texture_t> texture;
    cairo_t *cr = nullptr;
    cairo_surface_t *cairo_surface;
    PangoLayout *layout; // FIXME: g_object_unref
    PangoFontDescription *desc; // FIXME: pango_font_description_free
};

/* Icon loading functions */
namespace IconProvider
{
    using Icon = Glib::RefPtr<Gio::Icon>;

    namespace
    {
        std::string tolower(std::string str)
        {
            for (auto& c : str)
                c = std::tolower(c);
            return str;
        }
    }

    /* Gio::DesktopAppInfo
     *
     * Usually knowing the app_id, we can get a desktop app info from Gio
     * The filename is either the app_id + ".desktop" or lower_app_id + ".desktop" */
    Icon get_from_desktop_app_info(std::string app_id)
    {
        Glib::RefPtr<Gio::DesktopAppInfo> app_info;

        std::vector<std::string> prefixes = {
            "",
            "/usr/share/applications/",
            "/usr/share/applications/kde/",
            "/usr/share/applications/org.kde.",
            "/usr/local/share/applications/",
            "/usr/local/share/applications/org.kde.",
        };

        std::vector<std::string> app_id_variations = {
            app_id,
            tolower(app_id),
        };

        std::vector<std::string> suffixes = {
            "",
            ".desktop"
        };

        for (auto& prefix : prefixes)
        {
            for (auto& id : app_id_variations)
            {
                for (auto& suffix : suffixes)
                {
                    if (!app_info)
                    {
                        app_info = Gio::DesktopAppInfo
                            ::create_from_filename(prefix + id + suffix);
                    }
                }
            }
        }

        if (app_info) // success
            return app_info->get_icon();

        return Icon{};
    }

    /* Second method: Just look up the built-in icon theme,
     * perhaps some icon can be found there */
    std::string get_image_path_from_icon(std::string app_id)
    {
        auto icon = get_from_desktop_app_info(app_id);

        if (icon) {
            auto icon_string = icon->to_string();
            if (icon_string.c_str()[0] == '/') {
                if (Gio::File::create_for_path(icon_string)->query_exists()) {
                    return icon_string;
                }
            } else {
                auto path = "/usr/share/icons/Papirus-Dark/24x24/apps/" + icon_string + ".svg"; // FIXME: Don't hardcode
                if (Gio::File::create_for_path(path)->query_exists()) {
                    return path;
                }
            }
        }

        auto path = "/usr/share/icons/Papirus-Dark/24x24/apps/" + app_id + ".svg";
        if (Gio::File::create_for_path(path)->query_exists()) {
            return path;
        }

        return "/usr/share/icons/Papirus-Dark/24x24/mimetypes/application-x-executable.svg";
    }
};

class wayfire_simple_switcher : public wf::plugin_interface_t
{
    wf::option_wrapper_t<wf::keybinding_t> activate_key{"wwp-switcher/activate"};
    wf::option_wrapper_t<wf::color_t> background_color{"wwp-switcher/background_color"};
    wf::option_wrapper_t<wf::color_t> text_color{"wwp-switcher/text_color"};
    wf::option_wrapper_t<std::string> font{"wwp-switcher/font"};
    switcher_texture st;
    std::vector<wayfire_view> views; // all views on current viewport

    size_t current_view_index = 0;
    bool active = false;

  public:
    void init() override
    {
        grab_interface->name = "wwp-switcher";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;
        output->add_key(activate_key, &simple_switch);

        using namespace std::placeholders;
        grab_interface->callbacks.keyboard.mod =
            std::bind(std::mem_fn(&wayfire_simple_switcher::handle_mod),
                this, _1, _2);

        grab_interface->callbacks.cancel = [=] ()
        {
            switch_terminate();
        };

        Gio::init();
    }

    void handle_mod(uint32_t mod, uint32_t st)
    {
        bool mod_released =
            (mod == ((wf::keybinding_t)activate_key).get_modifiers() &&
                st == WLR_KEY_RELEASED);

        if (mod_released)
        {
            switch_terminate();
        }
    }

    void update_views()
    {
        views = output->workspace->get_views_on_workspace(
            output->workspace->get_current_workspace(), wf::WM_LAYERS);

        std::sort(views.begin(), views.end(), [] (wayfire_view& a, wayfire_view& b)
        {
            return a->last_focus_timestamp > b->last_focus_timestamp;
        });
    }

    wf::signal_connection_t cleanup_view = [=] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);

        size_t i = 0;
        for (; i < views.size() && views[i] != view; i++)
        {}

        if (i == views.size())
        {
            return;
        }

        views.erase(views.begin() + i);

        if (views.empty())
        {
            switch_terminate();

            return;
        }

        if (i <= current_view_index)
        {
            current_view_index =
                (current_view_index + views.size() - 1) % views.size();
        }
    };

    void cairo_clear(cairo_t *cr)
    {
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
    }

    void recreate_switcher_texture()
    {
        cairo_t *cr    = st.cr;
        cairo_surface_t *cairo_surface = st.cairo_surface;

        if (!cr)
        {
            /* Setup dummy context to get initial font size */
            cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            cr = cairo_create(cairo_surface);
            st.texture = std::make_unique<wf::simple_texture_t>();

            st.layout = pango_cairo_create_layout(cr);
        }

        st.desc = pango_font_description_from_string(std::string(font).c_str());

        st.rect.width = 480;
        st.rect.height = views.size() * 48 + 24;

        /* Recreate surface based on font size */
        cairo_destroy(cr);
        cairo_surface_destroy(cairo_surface);

        cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            st.rect.width, st.rect.height);
        cr = cairo_create(cairo_surface);

        pango_cairo_update_layout(cr, st.layout);

        st.cr = cr;
        st.cairo_surface = cairo_surface;

        auto workarea = output->workspace->get_workarea();
        st.rect.x = workarea.x + (workarea.width / 2 - st.rect.width / 2);
        st.rect.y = workarea.y + (workarea.height / 2 - st.rect.height / 2);
    }

    void render_switcher_texture()
    {
        recreate_switcher_texture();

        double xc = st.rect.width / 2;
        double yc = st.rect.height / 2;
        int x2, y2;
        cairo_t *cr = st.cr;

        cairo_clear(cr);

        x2 = st.rect.width;
        y2 = st.rect.height;

        cairo_set_source_rgba(cr,
            wf::color_t(background_color).r,
            wf::color_t(background_color).g,
            wf::color_t(background_color).b,
            wf::color_t(background_color).a);
        cairo_new_path(cr);
        cairo_move_to(cr, 0, y2);
        cairo_line_to(cr, 0, 0);
        cairo_line_to(cr, x2, 0);
        cairo_line_to(cr, x2, y2);
        cairo_close_path(cr);
        cairo_fill(cr);

        int width, height;
        pango_layout_get_size(st.layout, &width, &height);
        cairo_move_to(cr,
            xc - width / PANGO_SCALE / 2,
            yc - height / PANGO_SCALE / 2);

        wayfire_view view;
        for (long unsigned int i = 0; i < views.size(); i++)
        {
            view = views[i];

            std::string icon_path = IconProvider::get_image_path_from_icon(view->get_app_id());
            auto pixbuf = gdk_pixbuf_new_from_file_at_size(icon_path.c_str(), 24, 24, nullptr);

            gdk_cairo_set_source_pixbuf(cr, pixbuf, 24, i * 48 + 24);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_rectangle(cr, 24, i * 48 + 24, 24, 24);
            cairo_fill(cr);

            cairo_move_to(cr, 72, i * 48 + 24);

            cairo_set_source_rgba(cr,
                wf::color_t(text_color).r,
                wf::color_t(text_color).g,
                wf::color_t(text_color).b,
                wf::color_t(text_color).a);

            pango_font_description_set_weight(st.desc, i == current_view_index ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
            pango_layout_set_font_description(st.layout, st.desc);

            pango_layout_set_ellipsize(st.layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_width(st.layout, 384 * PANGO_SCALE);

            pango_layout_set_text(st.layout, view->get_title().c_str(), -1);

            pango_cairo_show_layout(cr, st.layout);
        }

        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(st.cairo_surface, *st.texture);
        OpenGL::render_end();

        output->render->damage_whole();
    }

    wf::signal_connection_t render_switcher{[this] (wf::signal_data_t *data)
        {
            const auto& workspace = static_cast<wf::stream_signal_t*>(data);
            auto damage = output->render->get_scheduled_damage() &
                output->render->get_ws_box(workspace->ws);
            auto og   = workspace->fb.geometry;
            auto rect = st.rect;

            rect.x += og.x;
            rect.y += og.y;

            OpenGL::render_begin(workspace->fb);
            for (auto& box : damage)
            {
                workspace->fb.logic_scissor(wlr_box_from_pixman_box(box));
                OpenGL::render_texture(wf::texture_t{st.texture->tex},
                    workspace->fb, rect, glm::vec4(1, 1, 1, 1),
                    OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
            }

            OpenGL::render_end();
        }
    };

    wf::key_callback simple_switch = [=] (auto)
    {
        if (active)
        {
            switch_next();

            return true;
        }

        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        update_views();

        if (views.size() < 1)
        {
            output->deactivate_plugin(grab_interface);

            return false;
        }

        current_view_index = 0;
        active = true;

        grab_interface->grab();
        switch_next();

        output->connect_signal("view-disappeared", &cleanup_view);

        output->render->connect_signal("workspace-stream-post", &render_switcher);

        return true;
    };

    void switch_terminate()
    {
        output->render->damage_whole();

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        output->focus_view(views[current_view_index], true);

        render_switcher.disconnect();

        active = false;

        output->disconnect_signal(&cleanup_view);
    }

    void switch_next()
    {
#define index current_view_index
        index = (index + 1) % views.size();
#undef index
        render_switcher_texture();
    }

    void fini() override
    {
        if (active)
        {
            switch_terminate();
        }

        output->rem_binding(&simple_switch);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_simple_switcher);
