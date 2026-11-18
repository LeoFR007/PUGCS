// Infraestrutura comum dos painéis.
#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include "modelo/Historico.hpp"

namespace gcs {

/// Um painel é uma visualização dos dados do veículo.
///
/// Repare que o painel NÃO escreve na tela: ele devolve o quadro pronto como
/// texto. Com isso o painel pode ser testado com um Historico de mentira, e
/// quem cuida do terminal (ou, no futuro, de uma janela de verdade) é o
/// Renderizador. É a separação clássica entre "o que mostrar" e "onde mostrar".
class Painel {
public:
    explicit Painel(std::string titulo) : titulo_(std::move(titulo)) {}
    virtual ~Painel() = default;

    const std::string& titulo() const { return titulo_; }

    /// Monta um quadro a partir do histórico de telemetria.
    virtual std::string quadro(const Historico& historico) const = 0;

    /// Limites dos alarmes visuais.
    static constexpr double BATERIA_BAIXA_PCT = 25.0;
    static constexpr double ROLAGEM_EXCESSIVA_GRAUS = 35.0;

protected:
    static bool bateria_baixa(const EstadoVeiculo& estado);
    static bool rolagem_excessiva(const EstadoVeiculo& estado);
    /// Uma linha com os alarmes ativos em vermelho (ou vazia). Sempre ocupa uma
    /// linha, para o quadro não mudar de altura e deixar sobras na tela.
    static std::string linha_de_alarmes(const EstadoVeiculo& estado);

private:
    std::string titulo_;
};

/// Redesenha um painel a uma taxa fixa, até alguém pedir para parar.
class Renderizador {
public:
    explicit Renderizador(const Historico& historico, double quadros_por_segundo = 10.0);

    /// Bloqueia desenhando `painel` até `parar` virar true.
    /// Pensado para rodar em uma thread separada da que lê o teclado.
    void exibir_enquanto(const Painel& painel, const std::atomic<bool>& parar) const;

private:
    const Historico& historico_;
    std::chrono::milliseconds intervalo_;
};

}  // namespace gcs
