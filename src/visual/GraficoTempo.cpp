#include "visual/GraficoTempo.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include "visual/Terminal.hpp"

namespace gcs {
namespace {

// Uma série só, então não há legenda: o título já diz o que está desenhado.
// A grade e os eixos ficam discretos para a linha de dados ter destaque.
const std::string COR_SERIE = terminal::frente(0x2A, 0x78, 0xD6);
const std::string COR_APAGADA = terminal::frente(0x82, 0x81, 0x7C);

constexpr char MARCA = '*';

/// Liga (coluna0, linha0) a (coluna1, linha1) na grade com o algoritmo de
/// Bresenham: assim uma subida rápida vira uma linha contínua, não pontos soltos.
void tracar(std::vector<std::string>& grade, int coluna0, int linha0, int coluna1, int linha1) {
    const int dx = std::abs(coluna1 - coluna0);
    const int dy = -std::abs(linha1 - linha0);
    const int passo_x = coluna0 < coluna1 ? 1 : -1;
    const int passo_y = linha0 < linha1 ? 1 : -1;
    int erro = dx + dy;
    while (true) {
        if (linha0 >= 0 && linha0 < static_cast<int>(grade.size()) && coluna0 >= 0 &&
            coluna0 < static_cast<int>(grade[0].size())) {
            grade[static_cast<std::size_t>(linha0)][static_cast<std::size_t>(coluna0)] = MARCA;
        }
        if (coluna0 == coluna1 && linha0 == linha1) {
            break;
        }
        const int dobro = 2 * erro;
        if (dobro >= dy) {
            erro += dy;
            coluna0 += passo_x;
        }
        if (dobro <= dx) {
            erro += dx;
            linha0 += passo_y;
        }
    }
}

std::string numero(double valor, int largura = 8, int decimais = 1) {
    std::ostringstream fluxo;
    fluxo << std::fixed << std::setprecision(decimais) << std::setw(largura) << valor;
    return fluxo.str();
}

}  // namespace

GraficoTempo::GraficoTempo(Campo campo, int largura, int altura, double janela_s)
    : Painel(campo.rotulo + " (" + campo.unidade + ")"),
      campo_(std::move(campo)),
      largura_(std::max(20, largura)),
      altura_(std::max(5, altura)),
      janela_s_(janela_s) {}

std::string GraficoTempo::quadro(const Historico& historico) const {
    std::ostringstream saida;
    const std::string cabecalho =
        std::string(terminal::NEGRITO) + titulo() + terminal::NORMAL;

    const Historico::Serie serie = historico.serie(campo_.extrator);
    if (serie.vazia()) {
        saida << cabecalho << terminal::LIMPAR_LINHA << "\n\n"
              << "aguardando telemetria..." << terminal::LIMPAR_LINHA << "\n";
        return saida.str();
    }

    // 1. Recorta a janela de tempo mais recente, descartando valores ainda
    //    desconhecidos (NaN): eles não são zero e não podem virar ponto.
    const double fim = serie.segundos.back();
    const double inicio = std::max(0.0, fim - janela_s_);
    std::vector<double> tempos;
    std::vector<double> valores;
    for (std::size_t i = 0; i < serie.segundos.size(); ++i) {
        if (serie.segundos[i] >= inicio && std::isfinite(serie.valores[i])) {
            tempos.push_back(serie.segundos[i]);
            valores.push_back(serie.valores[i]);
        }
    }
    if (valores.empty()) {
        saida << cabecalho << terminal::LIMPAR_LINHA << "\n\n"
              << "aguardando um valor válido de " << campo_.rotulo << "..."
              << terminal::LIMPAR_LINHA << "\n";
        return saida.str();
    }

    // 2. Escala vertical, com uma folga para a linha não encostar na borda.
    double minimo = *std::min_element(valores.begin(), valores.end());
    double maximo = *std::max_element(valores.begin(), valores.end());
    const double folga = std::max(0.5, (maximo - minimo) * 0.15);
    minimo -= folga;
    maximo += folga;

    // 3. Pinta os pontos em uma grade de caracteres, ligando cada um ao
    //    anterior para a curva ficar contínua.
    std::vector<std::string> grade(static_cast<std::size_t>(altura_),
                                   std::string(static_cast<std::size_t>(largura_), ' '));
    const double duracao = std::max(1e-6, fim - inicio);
    const double amplitude = std::max(1e-6, maximo - minimo);
    int coluna_anterior = 0;
    int linha_anterior = 0;
    for (std::size_t i = 0; i < tempos.size(); ++i) {
        const int coluna = static_cast<int>(
            std::lround((tempos[i] - inicio) / duracao * (largura_ - 1)));
        const int linha = static_cast<int>(
            std::lround((maximo - valores[i]) / amplitude * (altura_ - 1)));
        if (i == 0) {
            tracar(grade, coluna, linha, coluna, linha);
        } else {
            tracar(grade, coluna_anterior, linha_anterior, coluna, linha);
        }
        coluna_anterior = coluna;
        linha_anterior = linha;
    }

    // 4. Monta o texto final. Cada linha termina com LIMPAR_LINHA para apagar
    //    o que sobrou do quadro anterior.
    saida << cabecalho << "   atual: " << numero(valores.back(), 0) << ' '
          << campo_.unidade << terminal::LIMPAR_LINHA << "\n";
    saida << linha_de_alarmes(historico.ultimo());
    for (int linha = 0; linha < altura_; ++linha) {
        std::string rotulo(8, ' ');
        if (linha == 0) {
            rotulo = numero(maximo);
        } else if (linha == altura_ - 1) {
            rotulo = numero(minimo);
        } else if (linha == altura_ / 2) {
            rotulo = numero((maximo + minimo) / 2.0);
        }
        saida << COR_APAGADA << rotulo << " |" << terminal::NORMAL << COR_SERIE
              << grade[static_cast<std::size_t>(linha)] << terminal::NORMAL
              << terminal::LIMPAR_LINHA << "\n";
    }
    saida << COR_APAGADA << std::string(9, ' ') << '+'
          << std::string(static_cast<std::size_t>(largura_), '-') << terminal::NORMAL
          << terminal::LIMPAR_LINHA << "\n";
    saida << COR_APAGADA << std::string(10, ' ') << numero(inicio, 0) << " s"
          << std::string(static_cast<std::size_t>(std::max(1, largura_ - 20)), ' ')
          << numero(fim, 0) << " s" << terminal::NORMAL << terminal::LIMPAR_LINHA << "\n";
    saida << "\n" << COR_APAGADA << "[Enter] volta para o console" << terminal::NORMAL
          << terminal::LIMPAR_LINHA << "\n";
    return saida.str();
}

const std::map<std::string, GraficoTempo::Campo>& campos_disponiveis() {
    static const std::map<std::string, GraficoTempo::Campo> catalogo{
        {"altitude",
         {"Altitude relativa", "m",
          [](const EstadoVeiculo& estado) { return estado.posicao.altitude_relativa_m; }}},
        {"bateria",
         {"Bateria", "%",
          [](const EstadoVeiculo& estado) { return estado.bateria.percentual; }}},
        {"tensao",
         {"Tensão da bateria", "V",
          [](const EstadoVeiculo& estado) { return estado.bateria.tensao_v; }}},
        {"velocidade",
         {"Velocidade no solo", "m/s",
          [](const EstadoVeiculo& estado) { return estado.velocidade.solo_mps; }}},
        {"subida",
         {"Taxa de subida", "m/s",
          [](const EstadoVeiculo& estado) { return estado.velocidade.subida_mps; }}},
        {"satelites",
         {"Satélites do GPS", "sat",
          [](const EstadoVeiculo& estado) { return static_cast<double>(estado.satelites); }}},
        {"radio",
         {"Sinal do rádio", "%",
          [](const EstadoVeiculo& estado) {
              return estado.radio.sinal_percentual < 0.0 ? std::nan("")
                                                         : estado.radio.sinal_percentual;
          }}},
        {"distancia_home",
         {"Distância até a home", "m",
          [](const EstadoVeiculo& estado) { return estado.distancia_home_m(); }}},
        {"rumo",
         {"Rumo", "deg", [](const EstadoVeiculo& estado) { return estado.atitude.guinada; }}},
        {"rolagem",
         {"Rolagem", "deg", [](const EstadoVeiculo& estado) { return estado.atitude.rolagem; }}},
        {"arfagem",
         {"Arfagem", "deg", [](const EstadoVeiculo& estado) { return estado.atitude.arfagem; }}},
    };
    return catalogo;
}

std::string nomes_dos_campos() {
    std::string nomes;
    for (const auto& par : campos_disponiveis()) {
        nomes += (nomes.empty() ? "" : ", ") + par.first;
    }
    return nomes;
}

// Ideias opcionais de evolução para este painel —
//  * marcar o instante da decolagem com uma coluna vertical;
//  * mostrar duas grandezas comparáveis no mesmo gráfico (com legenda!);
//  * descobrir a largura real do terminal em vez de usar um valor fixo.

}  // namespace gcs
