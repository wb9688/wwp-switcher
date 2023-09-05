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
#include <wayfire/plugins/common/input-grab.hpp>
#include "wayfire/plugins/common/util.hpp"
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/workarea.hpp>

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


namespace wf
{
namespace scene
{
class simple_node_render_instance_t : public render_instance_t
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged =
        [=] (node_damage_signal *ev)
    {
        push_to_parent(ev->region);
    };

    node_t *self;
    damage_callback push_to_parent;
    std::shared_ptr<switcher_texture> st;
    int *x, *y, *w, *h;

  public:
    simple_node_render_instance_t(node_t *self, damage_callback push_dmg,
        int *x, int *y, int *w, int *h, std::shared_ptr<switcher_texture> st)
    {
        this->x    = x;
        this->y    = y;
        this->w    = w;
        this->h    = h;
        this->self = self;
        this->st = st;
        this->push_to_parent = push_dmg;
        self->connect(&on_node_damaged);
    }

    void schedule_instructions(
        std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage)
    {
        // We want to render ourselves only, the node does not have children
        instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region)
    {
        auto ol    = this->st;
        wlr_box og = {*x, *y, *w, *h};

        auto rect = st->rect;

        if (!st->texture)
            return;

        OpenGL::render_begin(target);
        for (auto& box : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));

            rect.x += og.x;
            rect.y += og.y;

            OpenGL::render_texture(wf::texture_t{st->texture->tex},
                target, rect, glm::vec4(1, 1, 1, 1),
                OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        }

        OpenGL::render_end();
    }
};


class simple_node_t : public node_t
{
    int x, y, w, h;

  public:
    std::shared_ptr<switcher_texture> st;
    simple_node_t(int x, int y, int w, int h) : node_t(false)
    {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        st = std::make_shared<switcher_texture>();
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        // push_damage accepts damage in the parent's coordinate system
        // If the node is a transformer, it may transform the damage. However,
        // this simple nodes does not need any transformations, so the push_damage
        // callback is just passed along.
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, &x, &y, &w, &h, st));
    }

    void do_push_damage(wf::region_t updated_region)
    {
        node_damage_signal ev;
        ev.region = updated_region;
        this->emit(&ev);
    }

    wf::geometry_t get_bounding_box() override
    {
        // Specify whatever geometry your node has
        return {x, y, w, h};
    }

    void set_position(int x, int y)
    {
        this->x = x;
        this->y = y;
    }

    void set_size(int w, int h)
    {
        this->w = w;
        this->h = h;
    }
};

std::shared_ptr<simple_node_t> add_simple_node(wf::output_t *output, int x, int y,
    int w, int h)
{
    auto subnode = std::make_shared<simple_node_t>(x, y, w, h);
    wf::scene::add_front(output->node_for_layer(wf::scene::layer::TOP), subnode);
    return subnode;
}

class wayfire_simple_switcher : public wf::per_output_plugin_instance_t, public wf::keyboard_interaction_t
{
    wf::option_wrapper_t<wf::keybinding_t> activate_key{"wwp-switcher/activate"};
    wf::option_wrapper_t<wf::color_t> background_color{"wwp-switcher/background_color"};
    wf::option_wrapper_t<wf::color_t> text_color{"wwp-switcher/text_color"};
    wf::option_wrapper_t<std::string> font{"wwp-switcher/font"};
    std::vector<std::vector<std::shared_ptr<simple_node_t>>> switcher_textures;
    std::vector<wayfire_toplevel_view> views; // all views on current viewport

    size_t current_view_index = 0;
    bool active = false;

    std::unique_ptr<wf::input_grab_t> input_grab;

    wf::plugin_activation_data_t grab_interface = {
        .name = "wwp-switcher",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR
    };


  public:
    void init() override
    {
        auto wsize = output->wset()->get_workspace_grid_size();
        switcher_textures.resize(wsize.width);
        for (int x = 0; x < wsize.width; x++)
        {
            switcher_textures[x].resize(wsize.height);
        }
        output->add_key(activate_key, &simple_switch);

        input_grab = std::make_unique<wf::input_grab_t>("wwp-switcher", output, this, nullptr, nullptr);
        grab_interface.cancel = [=] () { switch_terminate(); };

        Gio::init();
    }

    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event event) override
    {
        auto mod = wf::get_core().seat->modifier_from_keycode(event.keycode);
        if ((event.state == WLR_KEY_RELEASED) && (mod & ((wf::keybinding_t)activate_key).get_modifiers()))
            switch_terminate();
    }

    std::shared_ptr<switcher_texture> get_current_st()
    {
        auto wsize = output->wset()->get_workspace_grid_size();
        auto ws = output->wset()->get_current_workspace();
        auto og = output->get_relative_geometry();

        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                if (!switcher_textures[x][y])
                {
                    switcher_textures[x][y] = add_simple_node(output, x * og.width, y * og.height,
                        og.width, og.height);
                }
            }
        }

        return switcher_textures[ws.x][ws.y]->st;
    }

    void update_views()
    {
        views = output->wset()->get_views(
            wf::WSET_CURRENT_WORKSPACE | wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED);

        std::sort(views.begin(), views.end(), [] (wayfire_toplevel_view& a, wayfire_toplevel_view& b)
        {
            return wf::get_focus_timestamp(a) > wf::get_focus_timestamp(b);
        });
    }

    wf::signal::connection_t<wf::view_disappeared_signal> cleanup_view = [=] (wf::view_disappeared_signal *ev)
    {
        auto view = ev->view;
        if (!view)
        {
            return;
        }

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
        auto st = get_current_st();
        cairo_t *cr    = st->cr;
        cairo_surface_t *cairo_surface = st->cairo_surface;

        if (!cr)
        {
            /* Setup dummy context to get initial font size */
            cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            cr = cairo_create(cairo_surface);
            st->texture = std::make_unique<wf::simple_texture_t>();

            st->layout = pango_cairo_create_layout(cr);
        }

        st->desc = pango_font_description_from_string(std::string(font).c_str());

        st->rect.width = 480;
        st->rect.height = views.size() * 48 + 24;

        /* Recreate surface based on font size */
        cairo_destroy(cr);
        cairo_surface_destroy(cairo_surface);

        cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            st->rect.width, st->rect.height);
        cr = cairo_create(cairo_surface);

        pango_cairo_update_layout(cr, st->layout);

        st->cr = cr;
        st->cairo_surface = cairo_surface;

        auto workarea = output->workarea->get_workarea();
        st->rect.x = workarea.x + (workarea.width / 2 - st->rect.width / 2);
        st->rect.y = workarea.y + (workarea.height / 2 - st->rect.height / 2);
    }

    void render_switcher_texture()
    {
        recreate_switcher_texture();

        auto st = get_current_st();
        double xc = st->rect.width / 2;
        double yc = st->rect.height / 2;
        int x2, y2;
        cairo_t *cr = st->cr;

        cairo_clear(cr);

        x2 = st->rect.width;
        y2 = st->rect.height;

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
        pango_layout_get_size(st->layout, &width, &height);
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

            pango_font_description_set_weight(st->desc, i == current_view_index ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
            pango_layout_set_font_description(st->layout, st->desc);

            pango_layout_set_ellipsize(st->layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_width(st->layout, 384 * PANGO_SCALE);

            pango_layout_set_text(st->layout, view->get_title().c_str(), -1);

            pango_cairo_show_layout(cr, st->layout);
        }

        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(st->cairo_surface, *st->texture);
        OpenGL::render_end();

        output->render->damage_whole();
    }

    wf::key_callback simple_switch = [=] (auto)
    {
        if (active)
        {
            switch_next();

            return true;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        update_views();

        if (views.size() < 1)
        {
            output->deactivate_plugin(&grab_interface);

            return false;
        }

        current_view_index = 0;
        active = true;

        input_grab->grab_input(wf::scene::layer::OVERLAY);
        input_grab->set_wants_raw_input(true);
        switch_next();

        output->connect(&cleanup_view);

        return true;
    };

    void switcher_texture_destroy(std::shared_ptr<switcher_texture> st)
    {
        if (!st->cr)
        {
            return;
        }

        st->texture.reset();
        cairo_surface_destroy(st->cairo_surface);
        cairo_destroy(st->cr);
        st->cr = nullptr;
    }

    void switch_terminate()
    {
        output->render->damage_whole();

        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
        output->focus_view(views[current_view_index], true);

        active = false;

        output->disconnect(&cleanup_view);

        auto wsize = output->wset()->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                auto& st = switcher_textures[x][y]->st;
                switcher_texture_destroy(st);
                st.reset();
                wf::scene::remove_child(switcher_textures[x][y]);
                switcher_textures[x][y].reset();
            }
        }

        output->render->damage_whole();
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

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_simple_switcher>);
}
}
