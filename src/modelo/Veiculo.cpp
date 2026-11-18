#include "modelo/Veiculo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace gcs {

Veiculo::Veiculo(std::string nome) : nome_(std::move(nome)) {}

EstadoVeiculo Veiculo::estado() const {
    std::lock_guard<std::mutex> trava(mutex_);
    return estado_;  // cópia
}

std::string Veiculo::modo_de_voo() const {
    std::lock_guard<std::mutex> trava(mutex_);
    return estado_.modo_de_voo;
}

void Veiculo::registrar_observador(Observador observador) {
    std::lock_guard<std::mutex> trava(mutex_);
    observadores_.push_back(std::move(observador));
}

void Veiculo::atualizar(const std::function<void(EstadoVeiculo&)>& modificacao) {
    EstadoVeiculo copia;
    std::vector<Observador> observadores;
    {
        std::lock_guard<std::mutex> trava(mutex_);
        modificacao(estado_);
        estado_.instante = std::chrono::steady_clock::now();
        copia = estado_;
        observadores = observadores_;
    }
    // Os observadores são chamados FORA da trava: se um deles pedir estado()
    // de volta, não travamos o programa.
    for (const Observador& observador : observadores) {
        observador(copia);
    }
}

void Veiculo::validar_altura(double altura_m) {
    if (!std::isfinite(altura_m) || altura_m <= 0.0) {
        throw ErroDeVeiculo("altura inválida: precisa ser um número finito maior que zero");
    }
}

void Veiculo::validar_destino(double latitude, double longitude, double altitude_relativa_m) {
    if (!std::isfinite(latitude) || std::abs(latitude) > 90.0 || !std::isfinite(longitude) ||
        std::abs(longitude) > 180.0) {
        throw ErroDeVeiculo("coordenada inválida: latitude em [-90, 90] e longitude em [-180, 180]");
    }
    validar_altura(altitude_relativa_m);
}

std::ostream& operator<<(std::ostream& fluxo, const Veiculo& veiculo) {
    return fluxo << veiculo.nome() << ": " << veiculo.estado().resumo();
}

// ---------------------------------------------------------------------------
// VeiculoSimulado
// ---------------------------------------------------------------------------
VeiculoSimulado::VeiculoSimulado(std::string nome) : Veiculo(std::move(nome)) {}

VeiculoSimulado::~VeiculoSimulado() {
    parar_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void VeiculoSimulado::conectar() {
    // Desarmado, no chão e na home, com GPS e rádio em ordem.
    atualizar([](EstadoVeiculo& estado) {
        estado.modo_de_voo = "STABILIZE";
        estado.posicao.latitude = HOME_LATITUDE;
        estado.posicao.longitude = HOME_LONGITUDE;
        estado.home = Home{true, HOME_LATITUDE, HOME_LONGITUDE, 0.0};
        estado.satelites = 12;
        estado.radio = Radio{true, 90.0};
        estado.bateria = Bateria{12.6, 100.0};
    });
}

void VeiculoSimulado::iniciar_telemetria() {
    thread_ = std::thread(&VeiculoSimulado::laco_de_telemetria, this);
}

void VeiculoSimulado::laco_de_telemetria() {
    constexpr double METROS_POR_GRAU = 111320.0;
    const Instante inicio = std::chrono::steady_clock::now();
    Instante anterior = inicio;
    while (!parar_) {
        const Instante agora = std::chrono::steady_clock::now();
        const double t = segundos_entre(inicio, agora);
        const double dt = segundos_entre(anterior, agora);
        anterior = agora;

        // O laço só escreve a "física" (posição, velocidade, atitude, bateria);
        // `armado` e o modo pertencem aos comandos. A única exceção é o
        // desarme automático depois do pouso, que o ArduCopter também faz.
        atualizar([this, t, dt](EstadoVeiculo& estado) {
            Posicao& posicao = estado.posicao;

            // 1. Anda em linha reta até o destino (aproximação de Terra plana).
            estado.velocidade.solo_mps = 0.0;
            if (tem_destino_ && estado.em_voo) {
                const double norte = (latitude_destino_ - posicao.latitude) * METROS_POR_GRAU;
                const double leste = (longitude_destino_ - posicao.longitude) * METROS_POR_GRAU *
                                     std::cos(posicao.latitude * 3.14159265358979 / 180.0);
                const double distancia = std::hypot(norte, leste);
                const double passo = std::min(distancia, VELOCIDADE_HORIZONTAL_MPS * dt);
                if (distancia > 0.0) {
                    // Avança a fração passo/distância do caminho que falta.
                    posicao.latitude += (latitude_destino_ - posicao.latitude) * passo / distancia;
                    posicao.longitude += (longitude_destino_ - posicao.longitude) * passo / distancia;
                    estado.atitude.guinada = std::atan2(leste, norte) * 180.0 / 3.14159265358979;
                }
                estado.velocidade.solo_mps = dt > 0.0 ? passo / dt : 0.0;
                if (distancia - passo < 0.5) {  // chegou
                    tem_destino_ = false;
                    if (pousar_ao_chegar_) {
                        pousar_ao_chegar_ = false;
                        altura_alvo_m_ = 0.0;
                        pousando_ = true;
                    }
                }
            }

            // 2. Sobe ou desce em rampa até a altura alvo.
            const double antes = posicao.altitude_relativa_m;
            const double passo = VELOCIDADE_VERTICAL_MPS * dt;
            posicao.altitude_relativa_m =
                antes < altura_alvo_m_ ? std::min(altura_alvo_m_, antes + passo)
                                       : std::max(altura_alvo_m_, antes - passo);
            estado.velocidade.subida_mps = dt > 0.0 ? (posicao.altitude_relativa_m - antes) / dt : 0.0;
            estado.em_voo = posicao.altitude_relativa_m > 0.0;

            // 3. Pouso concluído: desarma sozinho.
            if (pousando_ && !estado.em_voo) {
                pousando_ = false;
                estado.armado = false;
            }

            // 4. Atitude oscilando (para o horizonte) e bateria gastando.
            estado.atitude.rolagem = 25.0 * std::sin(t / 2.0);
            estado.atitude.arfagem = 10.0 * std::sin(t / 3.0);
            if (!tem_destino_) {
                estado.atitude.guinada = std::fmod(t * 10.0, 360.0);
            }
            const double consumo = estado.em_voo ? 0.25 : 0.01;  // % por segundo
            estado.bateria.percentual = std::max(0.0, estado.bateria.percentual - consumo * dt);
            estado.bateria.tensao_v = 10.5 + 2.1 * estado.bateria.percentual / 100.0;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool VeiculoSimulado::no_ar(const EstadoVeiculo& estado) const {
    return estado.em_voo || altura_alvo_m_ > 0.0;
}

void VeiculoSimulado::alterar_modo_de_voo(const std::string& modo) {
    std::string maiusculo;
    for (char caractere : modo) {
        maiusculo += static_cast<char>(std::toupper(static_cast<unsigned char>(caractere)));
    }
    atualizar([&maiusculo](EstadoVeiculo& estado) { estado.modo_de_voo = maiusculo; });
}

// Os comandos verificam e agem dentro de UMA chamada a atualizar(): não há
// janela entre a verificação e a ação. Se lançarem, nada é alterado.

void VeiculoSimulado::armar() {
    atualizar([](EstadoVeiculo& estado) { estado.armado = true; });
}

void VeiculoSimulado::desarmar() {
    atualizar([this](EstadoVeiculo& estado) {
        if (no_ar(estado)) {
            throw ErroDeVeiculo("desarme recusado: o veículo está em voo (use 'pousar' antes)");
        }
        estado.armado = false;
    });
}

void VeiculoSimulado::decolar(double altura_m) {
    validar_altura(altura_m);
    atualizar([this, altura_m](EstadoVeiculo& estado) {
        if (!estado.armado) {
            throw ErroDeVeiculo("decolagem recusada: o veículo está desarmado (use 'armar' antes)");
        }
        if (no_ar(estado)) {
            throw ErroDeVeiculo("decolagem recusada: o veículo já está em voo");
        }
        altura_alvo_m_ = altura_m;
        estado.modo_de_voo = "GUIDED";  // como o ArduCopter faz ao decolar
    });
}

void VeiculoSimulado::pousar() {
    atualizar([this](EstadoVeiculo& estado) {
        if (!no_ar(estado)) {
            throw ErroDeVeiculo("pouso recusado: o veículo já está no chão");
        }
        tem_destino_ = false;
        pousar_ao_chegar_ = false;
        altura_alvo_m_ = 0.0;
        pousando_ = true;
        estado.modo_de_voo = "LAND";
    });
}

void VeiculoSimulado::retornar_para_casa() {
    atualizar([this](EstadoVeiculo& estado) {
        if (!no_ar(estado)) {
            throw ErroDeVeiculo("retorno recusado: o veículo não está em voo");
        }
        tem_destino_ = true;
        latitude_destino_ = estado.home.latitude;
        longitude_destino_ = estado.home.longitude;
        pousar_ao_chegar_ = true;
        estado.modo_de_voo = "RTL";
    });
}

void VeiculoSimulado::ir_para(double latitude, double longitude, double altitude_relativa_m) {
    validar_destino(latitude, longitude, altitude_relativa_m);
    atualizar([this, latitude, longitude, altitude_relativa_m](EstadoVeiculo& estado) {
        if (!no_ar(estado)) {
            throw ErroDeVeiculo("deslocamento recusado: o veículo não está em voo");
        }
        tem_destino_ = true;
        latitude_destino_ = latitude;
        longitude_destino_ = longitude;
        altura_alvo_m_ = altitude_relativa_m;
        pousar_ao_chegar_ = false;
        pousando_ = false;
        estado.modo_de_voo = "GUIDED";
    });
}

}  // namespace gcs
