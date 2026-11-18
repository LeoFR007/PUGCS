#include "modelo/Telemetria.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace gcs {

std::string EstadoVeiculo::resumo() const {
    std::ostringstream fluxo;
    fluxo << std::fixed << std::setprecision(1)
          << "modo=" << modo_de_voo
          << " armado=" << (armado ? "sim" : "nao")
          << " em_voo=" << (em_voo ? "sim" : "nao") << '\n'
          << "  alt_rel=" << posicao.altitude_relativa_m << " m"
          << " vel_solo=" << velocidade.solo_mps << " m/s"
          << " subida=" << velocidade.subida_mps << " m/s"
          << " dist_home=" << distancia_home_m() << " m" << '\n'
          << "  rolagem=" << atitude.rolagem << " deg"
          << " arfagem=" << atitude.arfagem << " deg"
          << " rumo=" << atitude.guinada << " deg" << '\n'
          << "  bat=" << bateria.percentual << " % (" << bateria.tensao_v << " V)"
          << " satelites=" << satelites
          << " radio=" << (radio.disponivel ? "ok" : "ausente");
    return fluxo.str();
}

double EstadoVeiculo::distancia_home_m() const {
    if (!home.definida) {
        return 0.0;
    }
    return distancia_horizontal_m(home.latitude, home.longitude, posicao.latitude,
                                  posicao.longitude);
}

double segundos_entre(Instante inicio, Instante fim) {
    return std::chrono::duration<double>(fim - inicio).count();
}

double distancia_horizontal_m(double latitude_a, double longitude_a, double latitude_b,
                              double longitude_b) {
    constexpr double RAIO_DA_TERRA_M = 6371000.0;
    constexpr double RADIANOS = 3.14159265358979323846 / 180.0;
    const double dlat = (latitude_b - latitude_a) * RADIANOS;
    const double dlon = (longitude_b - longitude_a) * RADIANOS;
    const double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(latitude_a * RADIANOS) * std::cos(latitude_b * RADIANOS) *
                         std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2.0 * RAIO_DA_TERRA_M * std::asin(std::sqrt(std::min(1.0, h)));
}

}  // namespace gcs
