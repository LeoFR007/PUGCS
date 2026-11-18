#include "modelo/Historico.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gcs {
namespace {

/// Colunas exportadas: nome + como tirar o número do estado. CSV e JSON usam a
/// mesma lista, então nunca discordam sobre o que exportam.
struct Coluna {
    const char* nome;
    double (*valor)(const EstadoVeiculo&);
};

const Coluna COLUNAS[] = {
    {"latitude", [](const EstadoVeiculo& e) { return e.posicao.latitude; }},
    {"longitude", [](const EstadoVeiculo& e) { return e.posicao.longitude; }},
    {"altitude_relativa_m", [](const EstadoVeiculo& e) { return e.posicao.altitude_relativa_m; }},
    {"velocidade_solo_mps", [](const EstadoVeiculo& e) { return e.velocidade.solo_mps; }},
    {"taxa_subida_mps", [](const EstadoVeiculo& e) { return e.velocidade.subida_mps; }},
    {"rolagem_deg", [](const EstadoVeiculo& e) { return e.atitude.rolagem; }},
    {"arfagem_deg", [](const EstadoVeiculo& e) { return e.atitude.arfagem; }},
    {"rumo_deg", [](const EstadoVeiculo& e) { return e.atitude.guinada; }},
    {"bateria_pct", [](const EstadoVeiculo& e) { return e.bateria.percentual; }},
    {"tensao_v", [](const EstadoVeiculo& e) { return e.bateria.tensao_v; }},
    {"satelites", [](const EstadoVeiculo& e) { return static_cast<double>(e.satelites); }},
    {"distancia_home_m", [](const EstadoVeiculo& e) { return e.distancia_home_m(); }},
};

/// Número com até 10 algarismos significativos; valor desconhecido (NaN) vira `se_desconhecido`.
std::string numero(double valor, const char* se_desconhecido) {
    if (!std::isfinite(valor)) {
        return se_desconhecido;
    }
    std::ostringstream fluxo;
    fluxo << std::setprecision(10) << valor;
    return fluxo.str();
}

std::ofstream abrir(const std::string& caminho) {
    std::ofstream arquivo(caminho);
    if (!arquivo) {
        throw std::runtime_error("não foi possível abrir '" + caminho + "' para escrita");
    }
    return arquivo;
}

}  // namespace


Historico::Historico(std::size_t capacidade) : capacidade_(capacidade) {}

void Historico::registrar(const EstadoVeiculo& estado) {
    std::lock_guard<std::mutex> trava(mutex_);
    estados_.push_back(estado);
    while (estados_.size() > capacidade_) {
        estados_.pop_front();
    }
}

std::size_t Historico::tamanho() const {
    std::lock_guard<std::mutex> trava(mutex_);
    return estados_.size();
}

bool Historico::vazio() const {
    std::lock_guard<std::mutex> trava(mutex_);
    return estados_.empty();
}

EstadoVeiculo Historico::ultimo(bool* ok) const {
    std::lock_guard<std::mutex> trava(mutex_);
    if (estados_.empty()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return EstadoVeiculo{};
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return estados_.back();
}

Historico::Serie Historico::serie(const Extrator& extrator) const {
    std::lock_guard<std::mutex> trava(mutex_);
    Serie resultado;
    if (estados_.empty()) {
        return resultado;
    }
    const Instante t0 = estados_.front().instante;
    resultado.segundos.reserve(estados_.size());
    resultado.valores.reserve(estados_.size());
    for (const EstadoVeiculo& estado : estados_) {
        resultado.segundos.push_back(segundos_entre(t0, estado.instante));
        resultado.valores.push_back(extrator(estado));
    }
    return resultado;
}

void Historico::limpar() {
    std::lock_guard<std::mutex> trava(mutex_);
    estados_.clear();
}

void Historico::exportar_csv(const std::string& caminho) const {
    std::ofstream arquivo = abrir(caminho);
    std::lock_guard<std::mutex> trava(mutex_);
    arquivo << "tempo_s,modo,armado,em_voo";
    for (const Coluna& coluna : COLUNAS) {
        arquivo << ',' << coluna.nome;
    }
    arquivo << '\n';
    for (const EstadoVeiculo& estado : estados_) {
        arquivo << numero(segundos_entre(estados_.front().instante, estado.instante), "") << ','
                << estado.modo_de_voo << ',' << estado.armado << ',' << estado.em_voo;
        for (const Coluna& coluna : COLUNAS) {
            arquivo << ',' << numero(coluna.valor(estado), "");
        }
        arquivo << '\n';
    }
}

void Historico::exportar_json(const std::string& caminho) const {
    std::ofstream arquivo = abrir(caminho);
    std::lock_guard<std::mutex> trava(mutex_);
    arquivo << "[\n";
    for (std::size_t i = 0; i < estados_.size(); ++i) {
        const EstadoVeiculo& estado = estados_[i];
        arquivo << "  {\"tempo_s\": "
                << numero(segundos_entre(estados_.front().instante, estado.instante), "null")
                << ", \"modo\": \"" << estado.modo_de_voo << '"'
                << ", \"armado\": " << (estado.armado ? "true" : "false")
                << ", \"em_voo\": " << (estado.em_voo ? "true" : "false");
        for (const Coluna& coluna : COLUNAS) {
            arquivo << ", \"" << coluna.nome << "\": " << numero(coluna.valor(estado), "null");
        }
        arquivo << '}' << (i + 1 < estados_.size() ? "," : "") << '\n';
    }
    arquivo << "]\n";
}

}  // namespace gcs
