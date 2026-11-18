#include "modelo/Drone.hpp"

// O plugin MavlinkPassthrough (sugerido no enunciado para os modos do
// ArduPilot) está marcado como obsoleto no MAVSDK 4.x: só é liberado com esta
// macro, e o aviso de "deprecated" é silenciado logo abaixo. Ele continua
// funcionando e é o caminho mais curto para enviar MAV_CMD_DO_SET_MODE.
#define MAVSDK_ENABLE_MAVLINK_C_API
#include <mavsdk/mavsdk.hpp>
#include <mavsdk/plugins/action/action.hpp>
#include <mavsdk/plugins/mavlink_passthrough/mavlink_passthrough.hpp>
#include <mavsdk/plugins/telemetry/telemetry.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <utility>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace gcs {
namespace {

/// Modos do ArduCopter: nome -> número (custom_mode), conforme
/// ArduCopter/mode.h. Usado para trocar de modo e para ler o modo atual.
const std::map<std::string, std::uint32_t>& modos_arducopter() {
    static const std::map<std::string, std::uint32_t> modos{
        {"STABILIZE", 0}, {"ACRO", 1},   {"ALT_HOLD", 2}, {"AUTO", 3},
        {"GUIDED", 4},    {"LOITER", 5}, {"RTL", 6},      {"CIRCLE", 7},
        {"LAND", 9},      {"POSHOLD", 16}, {"BRAKE", 17}, {"SMART_RTL", 21},
    };
    return modos;
}

std::string nome_do_modo(std::uint32_t numero) {
    for (const auto& [nome, valor] : modos_arducopter()) {
        if (valor == numero) {
            return nome;
        }
    }
    return "MODO_" + std::to_string(numero);
}

std::string para_maiusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(), [](unsigned char letra) {
        return static_cast<char>(std::toupper(letra));
    });
    return texto;
}

/// O MAVSDK imprime os enums com operator<<; é a forma mais simples de
/// transformar um FlightMode em texto.
template <typename T>
std::string para_texto(const T& valor) {
    std::ostringstream fluxo;
    fluxo << valor;
    return fluxo.str();
}

/// Converte a recusa do autopiloto em exceção, padrão de toda a classe.
void exigir_sucesso(mavsdk::Action::Result resultado, const std::string& operacao) {
    if (resultado != mavsdk::Action::Result::Success) {
        throw ErroDeVeiculo("o autopiloto recusou " + operacao + ": " + para_texto(resultado));
    }
}

}  // namespace

std::string normalizar_endereco(const std::string& endereco) {
    if (endereco.find("://") != std::string::npos) {
        // O MAVSDK 4.x exige a interface: "udpin://:14552" vira "udpin://0.0.0.0:14552".
        if (endereco.rfind("udpin://:", 0) == 0) {
            return "udpin://0.0.0.0:" + endereco.substr(9);
        }
        return endereco;  // já está no formato de URL
    }
    const std::size_t separador = endereco.find(':');
    if (separador == std::string::npos) {
        return endereco;
    }
    std::string esquema = endereco.substr(0, separador);
    const std::string resto = endereco.substr(separador + 1);
    // "tcp" é ambíguo para o MAVSDK; para falar com o SITL queremos "tcpout".
    if (esquema == "tcp") {
        esquema = "tcpout";
    }
    return esquema + "://" + resto;
}

/// Tudo que depende do MAVSDK vive aqui dentro.
class Drone::Impl {
public:
    Impl()
        : nucleo(mavsdk::Mavsdk::Configuration{mavsdk::ComponentType::GroundStation}) {}

    mavsdk::Mavsdk nucleo;
    std::shared_ptr<mavsdk::System> sistema;
    std::unique_ptr<mavsdk::Telemetry> telemetria;
    std::unique_ptr<mavsdk::Action> acoes;
    std::unique_ptr<mavsdk::MavlinkPassthrough> passthrough;  // modos do ArduPilot

    /// Plugin Action, ou ErroDeVeiculo se ainda não houver conexão.
    mavsdk::Action& acoes_conectadas(const std::string& operacao) {
        if (!acoes) {
            throw ErroDeVeiculo(operacao + " antes de conectar()");
        }
        return *acoes;
    }
};

Drone::Drone(std::string nome, std::string endereco)
    : Veiculo(std::move(nome)),
      impl_(std::make_unique<Impl>()),
      endereco_(std::move(endereco)) {}

Drone::~Drone() = default;  // precisa estar no .cpp por causa do pimpl

void Drone::conectar() {
    const std::string url = normalizar_endereco(endereco_);
    std::cout << "[" << nome() << "] conectando em " << url << " ..." << std::endl;

    const mavsdk::ConnectionResult resultado = impl_->nucleo.add_any_connection(url);
    if (resultado != mavsdk::ConnectionResult::Success) {
        throw ErroDeVeiculo("não foi possível abrir " + url + ": " + para_texto(resultado));
    }

    // O primeiro autopiloto que aparecer na rede MAVLink é o nosso drone.
    const auto sistema = impl_->nucleo.first_autopilot(TEMPO_LIMITE_S);
    if (!sistema) {
        throw ErroDeVeiculo(
            "nenhum autopiloto respondeu em " + url + ". O SITL do ArduPilot está rodando?");
    }

    impl_->sistema = sistema.value();
    impl_->telemetria = std::make_unique<mavsdk::Telemetry>(impl_->sistema);
    impl_->acoes = std::make_unique<mavsdk::Action>(impl_->sistema);
    impl_->passthrough = std::make_unique<mavsdk::MavlinkPassthrough>(impl_->sistema);
    std::cout << "[" << nome() << "] conectado." << std::endl;
}

void Drone::iniciar_telemetria() {
    if (!impl_->telemetria) {
        throw ErroDeVeiculo("iniciar_telemetria() antes de conectar()");
    }
    mavsdk::Telemetry& telemetria = *impl_->telemetria;

    // Cada assinatura é um callback chamado pela thread do MAVSDK. Tudo que
    // fazemos é traduzir o dado da biblioteca para o NOSSO modelo — assim o
    // resto do programa não depende do MAVSDK.
    // O modo vem do HEARTBEAT do autopiloto, pelo número do ArduCopter. (O
    // subscribe_flight_mode do MAVSDK usa nomes do PX4: "Offboard" para o
    // GUIDED, "Hold" para o LOITER... e confundiria quem digita `modo GUIDED`.)
    impl_->passthrough->subscribe_message(
        MAVLINK_MSG_ID_HEARTBEAT, [this](const mavlink_message_t& mensagem) {
            if (mensagem.compid != MAV_COMP_ID_AUTOPILOT1) {
                return;
            }
            const std::string texto =
                nome_do_modo(mavlink_msg_heartbeat_get_custom_mode(&mensagem));
            atualizar([&texto](EstadoVeiculo& estado) { estado.modo_de_voo = texto; });
        });

    telemetria.subscribe_attitude_euler([this](mavsdk::Telemetry::EulerAngle angulo) {
        atualizar([&angulo](EstadoVeiculo& estado) {
            estado.atitude.rolagem = angulo.roll_deg;
            estado.atitude.arfagem = angulo.pitch_deg;
            estado.atitude.guinada = angulo.yaw_deg;
        });
    });

    telemetria.subscribe_position([this](mavsdk::Telemetry::Position posicao) {
        atualizar([&posicao](EstadoVeiculo& estado) {
            estado.posicao.latitude = posicao.latitude_deg;
            estado.posicao.longitude = posicao.longitude_deg;
            estado.posicao.altitude_absoluta_m = posicao.absolute_altitude_m;
            estado.posicao.altitude_relativa_m = posicao.relative_altitude_m;
        });
    });

    telemetria.subscribe_battery([this](mavsdk::Telemetry::Battery bateria) {
        atualizar([&bateria](EstadoVeiculo& estado) {
            estado.bateria.tensao_v = bateria.voltage_v;
            // No MAVSDK 4.x remaining_percent já vem de 0 a 100 (nas versões
            // 1.x vinha de 0 a 1 — cuidado ao trocar de versão).
            estado.bateria.percentual = bateria.remaining_percent;
        });
    });

    telemetria.subscribe_armed([this](bool armado) {
        atualizar([armado](EstadoVeiculo& estado) { estado.armado = armado; });
    });

    telemetria.subscribe_in_air([this](bool em_voo) {
        atualizar([em_voo](EstadoVeiculo& estado) { estado.em_voo = em_voo; });
    });

    telemetria.subscribe_velocity_ned([this](mavsdk::Telemetry::VelocityNed velocidade) {
        atualizar([&velocidade](EstadoVeiculo& estado) {
            estado.velocidade.solo_mps = std::hypot(velocidade.north_m_s, velocidade.east_m_s);
            estado.velocidade.subida_mps = -velocidade.down_m_s;  // NED aponta para baixo
        });
    });

    telemetria.subscribe_gps_info([this](mavsdk::Telemetry::GpsInfo gps) {
        atualizar([&gps](EstadoVeiculo& estado) { estado.satelites = gps.num_satellites; });
    });

    telemetria.subscribe_home([this](mavsdk::Telemetry::HomePosition home) {
        atualizar([&home](EstadoVeiculo& estado) {
            estado.home = Home{true, home.latitude_deg, home.longitude_deg,
                               home.absolute_altitude_m};
        });
    });

    telemetria.subscribe_rc_status([this](mavsdk::Telemetry::RcStatus radio) {
        atualizar([&radio](EstadoVeiculo& estado) {
            estado.radio.disponivel = radio.is_available;
            estado.radio.sinal_percentual =
                std::isnan(radio.signal_strength_percent) ? -1.0 : radio.signal_strength_percent;
        });
    });

    // O ArduPilot só transmite cada mensagem se alguma GCS pedir. Atrás do
    // MAVProxy ele já pediu; conectados direto na porta 5760, ninguém pede e a
    // atitude, a posição e a bateria ficariam paradas em zero.
    using Taxa = std::pair<const char*, mavsdk::Telemetry::Result>;
    for (const Taxa& taxa : std::initializer_list<Taxa>{
             {"posição", telemetria.set_rate_position(5.0)},
             {"atitude", telemetria.set_rate_attitude_euler(10.0)},
             {"velocidade", telemetria.set_rate_velocity_ned(5.0)},
             {"bateria", telemetria.set_rate_battery(1.0)},
             {"GPS", telemetria.set_rate_gps_info(1.0)},
             {"rádio", telemetria.set_rate_rc_status(1.0)},
             {"em voo", telemetria.set_rate_in_air(2.0)}}) {
        if (taxa.second != mavsdk::Telemetry::Result::Success) {
            std::cerr << "[" << nome() << "] aviso: taxa de " << taxa.first
                      << " não ajustada (" << para_texto(taxa.second) << ")\n";
        }
    }
}

void Drone::alterar_modo_de_voo(const std::string& modo) {
    impl_->acoes_conectadas("alterar_modo_de_voo()");
    const std::string alvo = para_maiusculas(modo);
    const auto encontrado = modos_arducopter().find(alvo);
    if (encontrado == modos_arducopter().end()) {
        std::string disponiveis;
        for (const auto& par : modos_arducopter()) {
            disponiveis += (disponiveis.empty() ? "" : ", ") + par.first;
        }
        throw ErroDeVeiculo("modo '" + alvo + "' desconhecido; disponíveis: " + disponiveis);
    }

    // O MAVSDK só oferece alguns modos como ações de alto nível. Para cobrir
    // todos, mandamos MAV_CMD_DO_SET_MODE com o número do modo do ArduCopter;
    // send_command_long espera a resposta (ACK) do autopiloto.
    mavsdk::MavlinkPassthrough::CommandLong comando{};
    comando.target_sysid = impl_->sistema->get_system_id();
    comando.target_compid = MAV_COMP_ID_AUTOPILOT1;
    comando.command = MAV_CMD_DO_SET_MODE;
    comando.param1 = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
    comando.param2 = static_cast<float>(encontrado->second);
    const auto resultado = impl_->passthrough->send_command_long(comando);
    if (resultado != mavsdk::MavlinkPassthrough::Result::Success) {
        throw ErroDeVeiculo("o autopiloto recusou o modo " + alvo + ": " + para_texto(resultado));
    }
}

void Drone::esperar_telemetria(const std::function<bool(const EstadoVeiculo&)>& condicao,
                               const std::string& o_que) const {
    const Instante inicio = std::chrono::steady_clock::now();
    while (!condicao(estado())) {
        if (segundos_entre(inicio, std::chrono::steady_clock::now()) > TEMPO_CONFIRMACAO_S) {
            throw ErroDeVeiculo("o autopiloto aceitou " + o_que + ", mas a telemetria não confirmou");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Drone::armar() {
    exigir_sucesso(impl_->acoes_conectadas("armar()").arm(), "o armamento");
    // O ACK chega antes da telemetria `armado`. Esperamos ela confirmar para
    // que um `decolar` digitado logo em seguida não seja recusado por engano.
    esperar_telemetria([](const EstadoVeiculo& estado) { return estado.armado; }, "armar");
}

void Drone::desarmar() {
    mavsdk::Action& acoes = impl_->acoes_conectadas("desarmar()");
    if (estado().em_voo) {
        throw ErroDeVeiculo("desarme recusado: o veículo está em voo (use 'pousar' antes)");
    }
    exigir_sucesso(acoes.disarm(), "o desarme");
    esperar_telemetria([](const EstadoVeiculo& estado) { return !estado.armado; }, "desarmar");
}

void Drone::decolar(double altura_m) {
    validar_altura(altura_m);
    mavsdk::Action& acoes = impl_->acoes_conectadas("decolar()");
    const EstadoVeiculo atual = estado();
    if (!atual.armado) {
        throw ErroDeVeiculo("decolagem recusada: o veículo está desarmado (use 'armar' antes)");
    }
    if (atual.em_voo) {
        throw ErroDeVeiculo("decolagem recusada: o veículo já está em voo");
    }
    // O ArduCopter só decola em GUIDED; no ArduPilot, Action::takeoff() troca
    // para GUIDED sozinho. Decole logo depois de armar: parado no chão, o
    // ArduCopter desarma em 10 s (parâmetro DISARM_DELAY).
    exigir_sucesso(acoes.set_takeoff_altitude(static_cast<float>(altura_m)),
                   "a altura de decolagem");
    exigir_sucesso(acoes.takeoff(), "a decolagem");
}

void Drone::pousar() {
    mavsdk::Action& acoes = impl_->acoes_conectadas("pousar()");
    if (!estado().em_voo) {
        throw ErroDeVeiculo("pouso recusado: o veículo já está no chão");
    }
    exigir_sucesso(acoes.land(), "o pouso");
}

void Drone::retornar_para_casa() {
    mavsdk::Action& acoes = impl_->acoes_conectadas("retornar_para_casa()");
    if (!estado().em_voo) {
        throw ErroDeVeiculo("retorno recusado: o veículo não está em voo");
    }
    exigir_sucesso(acoes.return_to_launch(), "o retorno para casa");
}

void Drone::ir_para(double latitude, double longitude, double altitude_relativa_m) {
    validar_destino(latitude, longitude, altitude_relativa_m);
    mavsdk::Action& acoes = impl_->acoes_conectadas("ir_para()");
    const EstadoVeiculo atual = estado();
    if (!atual.em_voo) {
        throw ErroDeVeiculo("deslocamento recusado: o veículo não está em voo");
    }
    // goto_location quer altitude ABSOLUTA (acima do mar); a nossa interface
    // usa a relativa à home. E o ArduCopter só aceita alvos em GUIDED.
    const double altitude_da_home =
        atual.posicao.altitude_absoluta_m - atual.posicao.altitude_relativa_m;
    alterar_modo_de_voo("GUIDED");
    exigir_sucesso(acoes.goto_location(latitude, longitude,
                                       static_cast<float>(altitude_da_home + altitude_relativa_m),
                                       std::nanf("")),
                   "o deslocamento");
}

}  // namespace gcs
