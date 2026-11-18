#include "visual/Painel.hpp"

#include <cmath>
#include <iostream>
#include <thread>

#include "visual/Terminal.hpp"

namespace gcs {

bool Painel::bateria_baixa(const EstadoVeiculo& estado) {
    return estado.bateria.percentual <= BATERIA_BAIXA_PCT;  // NaN (desconhecida) dá falso
}

bool Painel::rolagem_excessiva(const EstadoVeiculo& estado) {
    return std::abs(estado.atitude.rolagem) >= ROLAGEM_EXCESSIVA_GRAUS;
}

std::string Painel::linha_de_alarmes(const EstadoVeiculo& estado) {
    const std::string alerta = terminal::fundo(0xC0, 0x1C, 0x28) + terminal::frente(0xFF, 0xFF, 0xFF);
    std::string linha;
    if (bateria_baixa(estado)) {
        linha += alerta + " ! BATERIA BAIXA " + terminal::NORMAL + " ";
    }
    if (rolagem_excessiva(estado)) {
        linha += alerta + " ! ROLAGEM EXCESSIVA " + terminal::NORMAL + " ";
    }
    return linha + terminal::LIMPAR_LINHA + "\n";
}

Renderizador::Renderizador(const Historico& historico, double quadros_por_segundo)
    : historico_(historico),
      intervalo_(std::chrono::milliseconds(
          static_cast<int>(1000.0 / (quadros_por_segundo > 0.0 ? quadros_por_segundo : 1.0)))) {}

void Renderizador::exibir_enquanto(const Painel& painel, const std::atomic<bool>& parar) const {
    std::cout << terminal::LIMPAR_TELA << terminal::ESCONDER_CURSOR;
    while (!parar) {
        // Volta ao canto e reescreve o quadro por cima do anterior.
        std::cout << terminal::INICIO << painel.quadro(historico_) << std::flush;
        std::this_thread::sleep_for(intervalo_);
    }
    std::cout << terminal::MOSTRAR_CURSOR << terminal::LIMPAR_TELA << std::flush;
}

}  // namespace gcs
