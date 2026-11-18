#include "visual/HorizonteArtificial.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "modelo/Telemetria.hpp"
#include "visual/Terminal.hpp"

namespace gcs {
namespace {

/// Quantos graus cada caractere vale. Como um caractere de terminal é mais ou
/// menos duas vezes mais alto do que largo, uma coluna vale metade de uma linha
/// — é isso que mantém o horizonte com a inclinação correta.
///
/// 5 graus por linha faz cada degrau de 10 graus da escada cair EXATAMENTE em
/// uma linha da tela com o drone nivelado. Com 4, o degrau ficaria a 2,5
/// linhas do centro e qualquer rolagem mínima o quebraria em duas linhas.
constexpr double GRAUS_POR_LINHA = 5.0;
constexpr double GRAUS_POR_COLUNA = 2.5;

constexpr double PI = 3.14159265358979323846;

/// O que há atrás de cada caractere.
enum class Fundo { Ceu, Chao, Horizonte, Caixa };
/// A cor do caractere desenhado em cima do fundo.
enum class Tinta { Padrao, Simbolo, Texto, Alerta };

/// Linhas da escada de arfagem: de 10 em 10 graus, ocupando a faixa
/// [INICIO, FIM] graus de cada lado do centro (as asas do símbolo ocupam 12–28).
constexpr double ESCADA_INICIO = 10.0;
constexpr double ESCADA_FIM = 27.5;
/// Só aparecem os degraus a até esta distância da arfagem atual, como em um
/// PFD de verdade: o resto da escada só poluiria a tela.
constexpr double ESCADA_ALCANCE = 25.0;
/// Marcas do arco de rolagem, em graus.
constexpr int MARCAS_DE_ROLAGEM[] = {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60};
/// Graus de rumo por coluna na fita do compasso.
constexpr double GRAUS_POR_COLUNA_RUMO = 2.0;

struct Celula {
    Fundo fundo = Fundo::Ceu;
    Tinta tinta = Tinta::Padrao;
    char glifo = ' ';
};

const std::string FUNDO_CEU = terminal::fundo(0x2A, 0x78, 0xD6);
const std::string FUNDO_CHAO = terminal::fundo(0x8C, 0x5A, 0x2B);
const std::string FUNDO_HORIZONTE = terminal::fundo(0xFC, 0xFC, 0xFB);
const std::string FUNDO_CAIXA = terminal::fundo(0x1A, 0x1A, 0x19);
const std::string FRENTE_PADRAO = terminal::frente(0xFC, 0xFC, 0xFB);
const std::string FRENTE_SIMBOLO = terminal::frente(0xED, 0xA1, 0x00);
const std::string FRENTE_TEXTO = terminal::frente(0xFC, 0xFC, 0xFB);
const std::string FRENTE_ALERTA = terminal::frente(0xFF, 0x3B, 0x3B);

const std::string& codigo(Fundo fundo) {
    switch (fundo) {
        case Fundo::Chao: return FUNDO_CHAO;
        case Fundo::Horizonte: return FUNDO_HORIZONTE;
        case Fundo::Caixa: return FUNDO_CAIXA;
        case Fundo::Ceu: break;
    }
    return FUNDO_CEU;
}

const std::string& codigo(Tinta tinta) {
    switch (tinta) {
        case Tinta::Simbolo: return FRENTE_SIMBOLO;
        case Tinta::Texto: return FRENTE_TEXTO;
        case Tinta::Alerta: return FRENTE_ALERTA;
        case Tinta::Padrao: break;
    }
    return FRENTE_PADRAO;
}

std::string graus(double valor) {
    std::ostringstream fluxo;
    fluxo << std::fixed << std::setprecision(1) << std::showpos << std::setw(7) << valor;
    return fluxo.str();
}

std::string com_uma_casa(double valor) {
    std::ostringstream fluxo;
    fluxo << std::fixed << std::setprecision(1) << valor;
    return fluxo.str();
}

/// Rótulo da fita de rumo para um múltiplo de 30 graus: pontos cardeais por
/// letra, o resto com três dígitos ("030", "120"...).
std::string rotulo_de_rumo(int graus) {
    switch (graus) {
        case 0: return "N";
        case 90: return "E";
        case 180: return "S";
        case 270: return "W";
        default: break;
    }
    std::ostringstream fluxo;
    fluxo << std::setw(3) << std::setfill('0') << graus;
    return fluxo.str();
}

/// Rumo em [0, 360).
double normalizar_rumo(double graus) {
    const double resto = std::fmod(graus, 360.0);
    return resto < 0.0 ? resto + 360.0 : resto;
}

}  // namespace

HorizonteArtificial::HorizonteArtificial(int largura, int altura)
    : Painel("Horizonte artificial"),
      largura_(std::max(40, largura)),
      altura_(std::max(11, altura | 1)) {}  // altura ímpar: existe uma linha central

std::string HorizonteArtificial::quadro(const Historico& historico) const {
    bool recebeu = false;
    const EstadoVeiculo estado = historico.ultimo(&recebeu);
    std::ostringstream saida;
    if (!recebeu) {
        saida << titulo() << terminal::LIMPAR_LINHA << "\n\n"
              << "aguardando telemetria..." << terminal::LIMPAR_LINHA << "\n";
        return saida.str();
    }

    // 1. Decide, caractere por caractere, se ali é céu, chão ou horizonte.
    const double seno = std::sin(estado.atitude.rolagem * PI / 180.0);
    const double cosseno = std::cos(estado.atitude.rolagem * PI / 180.0);
    const double centro_linha = (altura_ - 1) / 2.0;
    const double centro_coluna = (largura_ - 1) / 2.0;

    std::vector<std::vector<Celula>> tela(
        static_cast<std::size_t>(altura_),
        std::vector<Celula>(static_cast<std::size_t>(largura_)));

    for (int linha = 0; linha < altura_; ++linha) {
        for (int coluna = 0; coluna < largura_; ++coluna) {
            const double y = (centro_linha - linha) * GRAUS_POR_LINHA;
            const double x = (coluna - centro_coluna) * GRAUS_POR_COLUNA;
            const double u = -x * seno + y * cosseno + estado.atitude.arfagem;
            Celula& celula = tela[static_cast<std::size_t>(linha)][static_cast<std::size_t>(coluna)];
            if (std::abs(u) <= GRAUS_POR_LINHA * 0.6) {
                celula.fundo = Fundo::Horizonte;
            } else {
                celula.fundo = (u > 0.0) ? Fundo::Ceu : Fundo::Chao;
            }
        }
    }

    // Escreve um caractere por cima do desenho. `fundo` nulo mantém o céu/chão
    // que já estava ali — é o que faz os símbolos parecerem parte do HUD.
    const auto escrever = [&](int linha, int coluna, char glifo, Tinta tinta,
                              std::optional<Fundo> fundo = std::nullopt) {
        if (linha < 0 || linha >= altura_ || coluna < 0 || coluna >= largura_) {
            return;
        }
        Celula& celula = tela[static_cast<std::size_t>(linha)][static_cast<std::size_t>(coluna)];
        celula.glifo = glifo;
        celula.tinta = tinta;
        if (fundo.has_value()) {
            celula.fundo = *fundo;
        }
    };
    const auto escrever_texto = [&](int linha, int coluna, const std::string& texto, Tinta tinta,
                                    std::optional<Fundo> fundo = std::nullopt) {
        for (std::size_t i = 0; i < texto.size(); ++i) {
            escrever(linha, coluna + static_cast<int>(i), texto[i], tinta, fundo);
        }
    };
    /// Converte um ponto em "graus de tela" (x para a direita, y para cima, a
    /// partir do centro) para linha e coluna.
    const auto para_tela = [&](double x, double y) {
        return std::pair<int, int>{static_cast<int>(std::lround(centro_linha - y / GRAUS_POR_LINHA)),
                                   static_cast<int>(std::lround(centro_coluna + x / GRAUS_POR_COLUNA))};
    };

    const bool alarme_rolagem = rolagem_excessiva(estado);
    const bool alarme_bateria = bateria_baixa(estado);

    // 2. Escada de arfagem: uma linha a cada 10 graus, paralela ao horizonte.
    //    Em coordenadas do horizonte, `u` é a altura acima dele e `v` a posição
    //    ao longo dele; a linha de p graus é onde u == p.
    for (int linha = 0; linha < altura_; ++linha) {
        for (int coluna = 0; coluna < largura_; ++coluna) {
            const double y = (centro_linha - linha) * GRAUS_POR_LINHA;
            const double x = (coluna - centro_coluna) * GRAUS_POR_COLUNA;
            const double u = -x * seno + y * cosseno + estado.atitude.arfagem;
            const double v = x * cosseno + y * seno;
            const double degrau = std::round(u / 10.0) * 10.0;
            if (degrau != 0.0 && std::abs(degrau - estado.atitude.arfagem) <= ESCADA_ALCANCE &&
                // Intervalo meio aberto: um degrau que cai exatamente entre
                // duas linhas da tela é desenhado em uma só, não nas duas.
                u - degrau > -GRAUS_POR_LINHA / 2.0 && u - degrau <= GRAUS_POR_LINHA / 2.0 &&
                std::abs(v) >= ESCADA_INICIO && std::abs(v) <= ESCADA_FIM) {
                escrever(linha, coluna, '-', Tinta::Padrao);
            }
        }
    }
    //    O número de cada degrau, nas duas pontas.
    for (int degrau = -90; degrau <= 90; degrau += 10) {
        if (degrau == 0 || std::abs(degrau - estado.atitude.arfagem) > ESCADA_ALCANCE) {
            continue;
        }
        const double d = degrau - estado.atitude.arfagem;  // distância até o degrau
        const std::string numero = std::to_string(std::abs(degrau));
        for (const double v : {ESCADA_FIM + 3.0, -(ESCADA_FIM + 3.0)}) {
            const double x = v * cosseno - d * seno;
            const double y = v * seno + d * cosseno;
            const int coluna = para_tela(x, y).second;
            // Mesmo desempate do traço (intervalo meio aberto), para o número
            // ficar na mesma linha do degrau.
            const int linha = static_cast<int>(std::ceil(centro_linha - y / GRAUS_POR_LINHA - 0.5));
            // Só escreve dentro da área do horizonte (longe das bordas).
            if (linha > 0 && linha < altura_ - 3) {
                const int deslocamento = v > 0 ? 0 : -static_cast<int>(numero.size()) + 1;
                escrever_texto(linha, coluna + deslocamento, numero, Tinta::Padrao);
            }
        }
    }

    // 3. Arco de rolagem no topo: marcas fixas e um ponteiro que gira com o
    //    mundo. Inclinar para a direita leva o ponteiro para a esquerda, como
    //    no instrumento de verdade. Fica vermelho com rolagem excessiva.
    const double raio = (centro_linha - 1.0) * GRAUS_POR_LINHA;
    const Tinta tinta_arco = alarme_rolagem ? Tinta::Alerta : Tinta::Padrao;
    for (const int marca : MARCAS_DE_ROLAGEM) {
        const double a = marca * PI / 180.0;
        const auto [linha, coluna] = para_tela(raio * std::sin(a), raio * std::cos(a));
        const char glifo = marca == 0 ? '|' : (std::abs(marca) % 30 == 0 ? ':' : '.');
        escrever(linha, coluna, glifo, tinta_arco);
    }
    {
        const double angulo = std::clamp(-estado.atitude.rolagem, -70.0, 70.0) * PI / 180.0;
        const double r = raio - 1.2 * GRAUS_POR_LINHA;
        const auto [linha, coluna] = para_tela(r * std::sin(angulo), r * std::cos(angulo));
        escrever(linha, coluna, '^', alarme_rolagem ? Tinta::Alerta : Tinta::Simbolo);
    }

    // 4. O símbolo da aeronave, sempre fixo no centro: é a referência do piloto.
    const int linha_central = altura_ / 2;
    const int coluna_central = largura_ / 2;
    for (int distancia = 5; distancia <= 11; ++distancia) {
        escrever(linha_central, coluna_central - distancia, '=', Tinta::Simbolo);
        escrever(linha_central, coluna_central + distancia, '=', Tinta::Simbolo);
    }
    escrever(linha_central, coluna_central, 'o', Tinta::Simbolo);

    // 5. Fita de rumo (compasso) nas duas últimas linhas: o rumo atual fica no
    //    centro, marcado pelo '^'; a fita corre por baixo dele.
    const int linha_marcas = altura_ - 2;
    const int linha_ponteiro = altura_ - 1;
    for (int coluna = 0; coluna < largura_; ++coluna) {
        escrever(linha_marcas, coluna, ' ', Tinta::Texto, Fundo::Caixa);
        escrever(linha_ponteiro, coluna, ' ', Tinta::Texto, Fundo::Caixa);
    }
    const double rumo = normalizar_rumo(estado.atitude.guinada);
    const double meia_fita = (largura_ / 2.0) * GRAUS_POR_COLUNA_RUMO;
    const int primeiro = static_cast<int>(std::ceil((rumo - meia_fita) / 10.0)) * 10;
    for (int graus = primeiro; graus <= rumo + meia_fita; graus += 10) {
        const int coluna = coluna_central +
                           static_cast<int>(std::lround((graus - rumo) / GRAUS_POR_COLUNA_RUMO));
        const int normalizado = static_cast<int>(normalizar_rumo(graus));
        if (normalizado % 30 == 0) {
            const std::string rotulo = rotulo_de_rumo(normalizado);
            escrever_texto(linha_marcas, coluna - static_cast<int>(rotulo.size() / 2), rotulo,
                           Tinta::Texto, Fundo::Caixa);
        } else {
            escrever(linha_marcas, coluna, '.', Tinta::Texto, Fundo::Caixa);
        }
    }
    const std::string leitura_rumo = "^ " + std::to_string(static_cast<int>(std::lround(rumo)) % 360);
    escrever_texto(linha_ponteiro, coluna_central, leitura_rumo, Tinta::Simbolo, Fundo::Caixa);

    // 6. As leituras numéricas, em uma caixa escura logo acima da fita, para
    //    ficarem legíveis tanto sobre o céu quanto sobre o chão.
    const std::string bateria = std::isnan(estado.bateria.percentual)
                                    ? std::string("?")
                                    : com_uma_casa(estado.bateria.percentual) + " %";
    std::vector<std::pair<std::string, Tinta>> leituras{
        {" modo    " + estado.modo_de_voo, Tinta::Texto},
        {" rolagem " + graus(estado.atitude.rolagem) + " deg",
         alarme_rolagem ? Tinta::Alerta : Tinta::Texto},
        {" arfagem " + graus(estado.atitude.arfagem) + " deg", Tinta::Texto},
        {" rumo    " + graus(estado.atitude.guinada) + " deg", Tinta::Texto},
        {" alt     " + graus(estado.posicao.altitude_relativa_m) + " m", Tinta::Texto},
        {" bateria " + bateria, alarme_bateria ? Tinta::Alerta : Tinta::Texto},
    };
    std::size_t largura_caixa = 0;
    for (const auto& leitura : leituras) {
        largura_caixa = std::max(largura_caixa, leitura.first.size() + 1);
    }
    const int topo_caixa = linha_marcas - 1 - static_cast<int>(leituras.size());
    for (std::size_t i = 0; i < leituras.size(); ++i) {
        std::string texto = leituras[i].first;
        texto.resize(largura_caixa, ' ');  // deixa a caixa retangular
        escrever_texto(topo_caixa + static_cast<int>(i), 1, texto, leituras[i].second, Fundo::Caixa);
    }

    // 7. Emite a tela, trocando as cores só quando elas mudam de verdade.
    saida << terminal::NEGRITO << titulo() << terminal::NORMAL << terminal::LIMPAR_LINHA << "\n";
    for (const std::vector<Celula>& linha : tela) {
        const Fundo* fundo_atual = nullptr;
        const Tinta* tinta_atual = nullptr;
        for (const Celula& celula : linha) {
            if (fundo_atual == nullptr || celula.fundo != *fundo_atual) {
                saida << codigo(celula.fundo);
                fundo_atual = &celula.fundo;
            }
            if (tinta_atual == nullptr || celula.tinta != *tinta_atual) {
                saida << codigo(celula.tinta);
                tinta_atual = &celula.tinta;
            }
            saida << celula.glifo;
        }
        saida << terminal::NORMAL << terminal::LIMPAR_LINHA << "\n";
    }
    saida << linha_de_alarmes(estado)
          << terminal::frente(0x82, 0x81, 0x7C) << "[Enter] volta para o console"
          << terminal::NORMAL << terminal::LIMPAR_LINHA << "\n";
    return saida.str();
}

// Ideias opcionais de evolução:
//  * fitas de altitude e velocidade nas laterais;
//  * descobrir o tamanho real do terminal em vez de usar um valor fixo.

}  // namespace gcs
