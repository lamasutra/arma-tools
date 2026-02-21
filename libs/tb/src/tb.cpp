#include "armatools/tb.h"

#include <chrono>
#include <format>
#include <iomanip>
#include <sstream>

namespace armatools::tb {

std::string p3d_base_name(const std::string& s) {
    std::string result = s;
    auto pos = result.find_last_of("\\/");
    if (pos != std::string::npos) result = result.substr(pos + 1);
    auto dot = result.rfind('.');
    if (dot != std::string::npos) result = result.substr(0, dot);
    return result;
}

uint32_t sdbm_hash(const std::string& s) {
    uint32_t h = 0;
    for (char ch : s) {
        auto c = static_cast<uint32_t>(static_cast<unsigned char>(ch));
        h = c + (h << 6) + (h << 16) - h;
    }
    return h;
}

std::string xml_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default:  out += c; break;
        }
    }
    return out;
}

void write_tml(std::ostream& w, const std::string& library_name,
               const std::vector<std::string>& models,
               const std::unordered_map<std::string, ModelMeta>* meta,
               const CategoryStyle& style,
               const std::unordered_map<std::string, std::string>* name_overrides) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto* tm = std::localtime(&time_t);
    std::ostringstream ts;
    ts << std::put_time(tm, "%m/%d/%y %H:%M:%S");
    std::string date_str = ts.str();

    w << "<?xml version=\"1.0\" ?>\n";
    w << std::format("<Library name=\"{}\" shape=\"{}\" default_fill=\"{}\" default_outline=\"{}\" tex=\"0\">\n",
                     library_name, style.shape, style.fill, style.outline);

    for (const auto& model : models) {
        std::string name;
        if (name_overrides) {
            auto it = name_overrides->find(model);
            name = (it != name_overrides->end()) ? it->second : p3d_base_name(model);
        } else {
            name = p3d_base_name(model);
        }
        w << "    <Template>\n";
        w << "        <Name>" << xml_esc(name) << "</Name>\n";
        w << "        <File>" << xml_esc(model) << "</File>\n";
        w << "        <Date>" << date_str << "</Date>\n";
        w << "        <Archive></Archive>\n";
        w << std::format("        <Fill>{}</Fill>\n", style.fill);
        w << std::format("        <Outline>{}</Outline>\n", style.outline);
        w << "        <Scale>1.000000</Scale>\n";
        // TB template libraries store hash text as signed 32-bit decimal.
        // Values above INT_MAX must be emitted as negative numbers (two's complement).
        int32_t signed_hash = static_cast<int32_t>(sdbm_hash(name));
        w << std::format("        <Hash>{}</Hash>\n", signed_hash);
        w << "        <ScaleRandMin>0.000000</ScaleRandMin>\n";
        w << "        <ScaleRandMax>0.000000</ScaleRandMax>\n";
        w << "        <YawRandMin>0.000000</YawRandMin>\n";
        w << "        <YawRandMax>0.000000</YawRandMax>\n";
        w << "        <PitchRandMin>0.000000</PitchRandMin>\n";
        w << "        <PitchRandMax>0.000000</PitchRandMax>\n";
        w << "        <RollRandMin>0.000000</RollRandMin>\n";
        w << "        <RollRandMax>0.000000</RollRandMax>\n";
        w << "        <TexLLU>0.000000</TexLLU>\n";
        w << "        <TexLLV>0.000000</TexLLV>\n";
        w << "        <TexURU>1.000000</TexURU>\n";
        w << "        <TexURV>1.000000</TexURV>\n";

        const ModelMeta* m_ptr = nullptr;
        if (meta) {
            auto it = meta->find(model);
            if (it != meta->end()) m_ptr = &it->second;
        }

        if (m_ptr) {
            w << std::format("        <BBRadius>{:f}</BBRadius>\n", m_ptr->bb_radius);
            w << std::format("        <BBHScale>{:f}</BBHScale>\n", m_ptr->bb_hscale);
            w << "        <AutoCenter>0</AutoCenter>\n";
            w << "        <XShift>0.000000</XShift>\n";
            w << "        <YShift>0.000000</YShift>\n";
            w << "        <ZShift>0.000000</ZShift>\n";
            w << std::format("        <Height>{:f}</Height>\n", m_ptr->height);
            w << std::format("        <BoundingMin X=\"{:f}\" Y=\"{:f}\" Z=\"{:f}\" />\n",
                             m_ptr->bbox_min[0], m_ptr->bbox_min[1], m_ptr->bbox_min[2]);
            w << std::format("        <BoundingMax X=\"{:f}\" Y=\"{:f}\" Z=\"{:f}\" />\n",
                             m_ptr->bbox_max[0], m_ptr->bbox_max[1], m_ptr->bbox_max[2]);
            w << std::format("        <BoundingCenter X=\"{:f}\" Y=\"{:f}\" Z=\"{:f}\" />\n",
                             m_ptr->bbox_center[0], m_ptr->bbox_center[1], m_ptr->bbox_center[2]);
        } else {
            w << "        <BBRadius>-1.000000</BBRadius>\n";
            w << "        <BBHScale>1.000000</BBHScale>\n";
            w << "        <AutoCenter>0</AutoCenter>\n";
            w << "        <XShift>0.000000</XShift>\n";
            w << "        <YShift>0.000000</YShift>\n";
            w << "        <ZShift>0.000000</ZShift>\n";
            w << "        <Height>0.000000</Height>\n";
            w << "        <BoundingMin X=\"999.000000\" Y=\"999.000000\" Z=\"999.000000\" />\n";
            w << "        <BoundingMax X=\"-999.000000\" Y=\"-999.000000\" Z=\"-999.000000\" />\n";
            w << "        <BoundingCenter X=\"0.000000\" Y=\"0.000000\" Z=\"0.000000\" />\n";
        }

        w << "        <Placement></Placement>\n";
        w << "    </Template>\n";
    }
    w << "</Library>\n";
}

} // namespace armatools::tb
