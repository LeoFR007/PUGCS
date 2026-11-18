// Classe base de todos os veículos conhecidos pela estação de controle.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "modelo/Telemetria.hpp"

namespace gcs {

/// Erro ao falar com o veículo (conexão, comando rejeitado...).
class ErroDeVeiculo : public std::runtime_error {
public:
    explicit ErroDeVeiculo(const std::string& mensagem) : std::runtime_error(mensagem) {}
};

/// Operação que ainda é exercício da disciplina. O console trata esta exceção
/// de forma amigável, então dá para registrar o esqueleto de um recurso antes
/// de implementá-lo.
class NaoImplementado : public std::logic_error {
public:
    explicit NaoImplementado(const std::string& o_que)
        : std::logic_error(o_que) {}
};

/// Contrato comum a qualquer veículo: multirrotor, asa fixa, barco, simulador.
///
/// A classe base resolve três responsabilidades genéricas:
///
///  1. guardar o último EstadoVeiculo conhecido, com acesso seguro entre threads;
///  2. avisar os interessados a cada atualização (padrão *Observer*) — é assim
///     que o histórico e os painéis recebem dados sem conhecer o MAVSDK;
///  3. declarar as operações que toda subclasse precisa oferecer.
///
/// O que é específico de um protocolo fica nas subclasses: veja gcs::Drone
/// (MAVSDK + ArduPilot) e gcs::VeiculoSimulado (dados falsos para testes).
class Veiculo {
public:
    /// Quem quer ser avisado quando o estado muda.
    /// Atenção: é chamado na thread da telemetria, não na do console.
    using Observador = std::function<void(const EstadoVeiculo&)>;

    explicit Veiculo(std::string nome);
    virtual ~Veiculo() = default;

    Veiculo(const Veiculo&) = delete;
    Veiculo& operator=(const Veiculo&) = delete;

    // --- leitura de estado -------------------------------------------------
    const std::string& nome() const { return nome_; }
    /// Cópia do último estado conhecido (segura para usar em qualquer thread).
    EstadoVeiculo estado() const;
    /// Modo de voo atual, p.ex. "STABILIZE", "GUIDED", "RTL".
    std::string modo_de_voo() const;

    // --- observer ----------------------------------------------------------
    void registrar_observador(Observador observador);

    // --- operações que toda subclasse precisa implementar ------------------
    /// Estabelece a conexão; só retorna quando o veículo responder.
    /// Lança ErroDeVeiculo em caso de falha.
    virtual void conectar() = 0;
    /// Assina os fluxos de telemetria (não bloqueia: os dados chegam por callback).
    virtual void iniciar_telemetria() = 0;
    /// Pede ao autopiloto a troca do modo de voo.
    virtual void alterar_modo_de_voo(const std::string& modo) = 0;

    // --- comandos de voo ---------------------------------------------------
    // Todos lançam ErroDeVeiculo quando uma pré-condição falha ou o veículo
    // recusa, e retornam assim que o pedido é aceito: o movimento em si
    // aparece depois, na telemetria.

    /// Arma os motores. Ao retornar, estado().armado é verdadeiro.
    virtual void armar() = 0;
    /// Desarma os motores. Só no chão.
    virtual void desarmar() = 0;
    /// Decola até `altura_m` metros acima da home. Exige altura > 0 e veículo armado.
    virtual void decolar(double altura_m) = 0;
    /// Pousa no ponto atual; o veículo desarma sozinho ao tocar o chão.
    virtual void pousar() = 0;
    /// Volta para a home e pousa (RTL). Só em voo.
    virtual void retornar_para_casa() = 0;
    /// Voa até a coordenada, na altitude relativa à home. Só em voo.
    virtual void ir_para(double latitude, double longitude, double altitude_relativa_m) = 0;

protected:
    /// Modifica o estado e notifica os observadores.
    ///
    /// Usada pelas subclasses quando a telemetria chega:
    ///
    ///     atualizar([&](EstadoVeiculo& e) { e.modo_de_voo = "GUIDED"; });
    ///
    /// A modificação roda com a trava tomada: ler e escrever o estado dentro
    /// dela é atômico. Se ela lançar uma exceção, nada é notificado.
    void atualizar(const std::function<void(EstadoVeiculo&)>& modificacao);

    /// Validações comuns a todas as subclasses; lançam ErroDeVeiculo.
    static void validar_altura(double altura_m);
    static void validar_destino(double latitude, double longitude, double altitude_relativa_m);

private:
    std::string nome_;
    mutable std::mutex mutex_;                // protege estado_ e observadores_
    EstadoVeiculo estado_;
    std::vector<Observador> observadores_;
};

/// Permite `std::cout << veiculo` (usado pelo comando `status`).
std::ostream& operator<<(std::ostream& fluxo, const Veiculo& veiculo);

/// Veículo falso: gera telemetria sintética, sem precisar do SITL ligado.
///
/// Serve de segundo exemplo de herança — a estação de controle não sabe (nem
/// precisa saber) se está falando com o ArduPilot ou com esta classe. Use
/// `./mini_gcs --simulado` para desenvolver a interface no avião, no ônibus,
/// em qualquer lugar.
class VeiculoSimulado : public Veiculo {
public:
    explicit VeiculoSimulado(std::string nome = "simulado");
    ~VeiculoSimulado() override;

    void conectar() override;
    void iniciar_telemetria() override;
    void alterar_modo_de_voo(const std::string& modo) override;
    void armar() override;
    void desarmar() override;
    void decolar(double altura_m) override;
    void pousar() override;
    void retornar_para_casa() override;
    void ir_para(double latitude, double longitude, double altitude_relativa_m) override;

    /// Home fictícia (campus da PUCRS) e velocidades da simulação.
    static constexpr double HOME_LATITUDE = -30.0593;
    static constexpr double HOME_LONGITUDE = -51.1729;
    static constexpr double VELOCIDADE_VERTICAL_MPS = 2.0;
    static constexpr double VELOCIDADE_HORIZONTAL_MPS = 5.0;

private:
    void laco_de_telemetria();
    /// Em voo ou já subindo: logo após `decolar` a altitude ainda é zero.
    bool no_ar(const EstadoVeiculo& estado) const;

    std::atomic<bool> parar_{false};
    std::thread thread_;

    // O "piloto automático" simulado. Estes campos só são lidos e escritos
    // DENTRO de atualizar(), ou seja, protegidos pela mesma trava do estado:
    // o laço de telemetria e os comandos do console nunca se atropelam.
    double altura_alvo_m_ = 0.0;
    bool tem_destino_ = false;
    double latitude_destino_ = 0.0;
    double longitude_destino_ = 0.0;
    bool pousar_ao_chegar_ = false;  // RTL: pousa quando chegar na home
    bool pousando_ = false;          // desarma quando tocar o chão
};

}  // namespace gcs
