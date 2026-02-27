#pragma once

#include "render_domain/rd_backend_abi.h"
#include "ui_domain/ui_backend_abi.h"

#include <memory>
#include <string>

namespace render_domain {

struct UiRenderBridgeInfo {
    std::string renderer_backend;
    std::string bridge_name;
    bool available = false;
    std::string reason;
};

class IUiRenderBridge {
public:
    virtual ~IUiRenderBridge() = default;

    [[nodiscard]] virtual UiRenderBridgeInfo info() const = 0;
    virtual int begin_frame() = 0;
    virtual int submit_draw_data(const ui_draw_data_v1* draw_data) = 0;
    virtual int draw_overlay() = 0;
    virtual int end_frame() = 0;
    virtual int render_in_current_context(int viewport_width, int viewport_height) = 0;
    [[nodiscard]] virtual const ui_render_bridge_v1* bridge_abi() const = 0;
};

std::shared_ptr<IUiRenderBridge> make_ui_render_bridge_for_backend(const std::string& backend_id);

}  // namespace render_domain
