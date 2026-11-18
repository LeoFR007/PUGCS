// Testes de unidade do Historico e dos painéis (README, item 6).
//
// Sem framework externo, para não mudar a instalação do projeto: cada TESTE é
// uma função registrada em uma lista, e o main roda todas.
//
//     cmake --build build && ctest --test-dir build --output-on-failure
//
// Os painéis são funções puras do histórico: dá para testá-los com um histórico
// montado à mão, sem terminal, sem SITL e sem thread.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "modelo/Historico.hpp"
#include "visual/GraficoTempo.hpp"
#include "visual/HorizonteArtificial.hpp"

namespace {

// ---------------------------------------------------------------------------
// Mini framework
// ---------------------------------------------------------------------------
std::vector<std::pair<std::string, std::function<void()>>>& casos() {
    static std::vector<std::pair<std::string, std::function<void()>>> lista;
    return lista;
}

struct Registro {
    Registro(const char* nome, void (*corpo)()) { casos().emplace_back(nome, corpo); }
};

#define TESTE(nome)                               \
    void nome();                                  \
    const Registro registro_##nome{#nome, &nome}; \
    void nome()

#define VERIFICAR(condicao)                                                                   \
    do {                                                                                      \
        if (!(condicao)) {                                                                    \
            throw std::runtime_error("linha " + std::to_string(__LINE__) + ": " #condicao);   \
        }                                                                                     \
    } while (false)

// ---------------------------------------------------------------------------
// Auxiliares
// ---------------------------------------------------------------------------
using namespace gcs;

const Instante T0 = std::chrono::steady_clock::now();

/// Estado `segundos` depois de T0, com a altitude pedida e bateria em 80 %.
EstadoVeiculo estado_em(double segundos, double altitude = 0.0) {
    EstadoVeiculo estado;
    estado.instante = T0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(segundos));
    estado.posicao.altitude_relativa_m = altitude;
    estado.bateria = Bateria{12.0, 80.0};
    return estado;
}

/// Remove as sequências de escape ANSI, sobrando só o texto visível.
std::string sem_ansi(const std::string& texto) {
    static const std::regex ansi("\x1b\\[[0-9;?]*[A-Za-z]");
    return std::regex_replace(texto, ansi, "");
}

std::vector<std::string> linhas(const std::string& texto) {
    std::vector<std::string> resultado;
    std::istringstream fluxo(texto);
    for (std::string linha; std::getline(fluxo, linha);) {
        resultado.push_back(linha);
    }
    return resultado;
}

bool contem(const std::string& texto, const std::string& trecho) {
    return texto.find(trecho) != std::string::npos;
}

std::string ler_arquivo(const std::string& caminho) {
    std::ifstream arquivo(caminho);
    std::ostringstream conteudo;
    conteudo << arquivo.rdbuf();
    return conteudo.str();
}

const GraficoTempo::Campo& campo(const std::string& nome) { return campos_disponiveis().at(nome); }

// ---------------------------------------------------------------------------
// Historico
// ---------------------------------------------------------------------------
TESTE(historico_registra_e_conta) {
    Historico historico;
    VERIFICAR(historico.vazio());
    historico.registrar(estado_em(0));
    historico.registrar(estado_em(1));
    VERIFICAR(historico.tamanho() == 2);
}

TESTE(historico_descarta_os_mais_antigos) {
    Historico historico(3);
    for (int i = 0; i < 5; ++i) {
        historico.registrar(estado_em(i, i));
    }
    VERIFICAR(historico.tamanho() == 3);
    const auto serie = historico.serie([](const EstadoVeiculo& e) { return e.posicao.altitude_relativa_m; });
    VERIFICAR(serie.valores.front() == 2.0 && serie.valores.back() == 4.0);
}

TESTE(historico_ultimo_avisa_quando_vazio) {
    Historico historico;
    bool ok = true;
    historico.ultimo(&ok);
    VERIFICAR(!ok);
    historico.registrar(estado_em(0, 7.5));
    VERIFICAR(historico.ultimo(&ok).posicao.altitude_relativa_m == 7.5 && ok);
}

TESTE(historico_serie_conta_o_tempo_desde_a_primeira_amostra) {
    Historico historico;
    historico.registrar(estado_em(10.0));
    historico.registrar(estado_em(11.0));
    historico.registrar(estado_em(12.5));
    const auto serie = historico.serie([](const EstadoVeiculo&) { return 0.0; });
    VERIFICAR(std::abs(serie.segundos[0]) < 1e-6);
    VERIFICAR(std::abs(serie.segundos[2] - 2.5) < 1e-6);
}

TESTE(historico_limpar_esvazia) {
    Historico historico;
    historico.registrar(estado_em(0));
    historico.limpar();
    VERIFICAR(historico.vazio());
}

TESTE(historico_aguenta_varias_threads) {
    Historico historico(2000);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&historico] {
            for (int i = 0; i < 1000; ++i) {
                historico.registrar(estado_em(i));
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    VERIFICAR(historico.tamanho() == 2000);
}

TESTE(historico_exporta_csv_e_json) {
    Historico historico;
    EstadoVeiculo primeiro = estado_em(0, 0.0);
    primeiro.bateria = Bateria{};  // desconhecida: vira campo vazio / null
    historico.registrar(primeiro);
    historico.registrar(estado_em(2, 10.0));

    const std::string csv = "teste_mini_gcs.csv";
    historico.exportar_csv(csv);
    const auto texto = linhas(ler_arquivo(csv));
    std::remove(csv.c_str());
    VERIFICAR(texto.size() == 3);  // cabeçalho + 2 amostras
    VERIFICAR(texto[0].rfind("tempo_s,modo,armado,em_voo", 0) == 0);
    VERIFICAR(texto[2].rfind("2,DESCONHECIDO,0,0", 0) == 0);
    VERIFICAR(!contem(texto[1], "nan"));

    const std::string json = "teste_mini_gcs.json";
    historico.exportar_json(json);
    const std::string conteudo = ler_arquivo(json);
    std::remove(json.c_str());
    VERIFICAR(conteudo.front() == '[');
    VERIFICAR(contem(conteudo, "\"bateria_pct\": null"));
    VERIFICAR(contem(conteudo, "\"altitude_relativa_m\": 10"));

    bool lancou = false;
    try {
        historico.exportar_csv("/pasta/que/nao/existe/voo.csv");
    } catch (const std::runtime_error&) {
        lancou = true;
    }
    VERIFICAR(lancou);
}

// ---------------------------------------------------------------------------
// GraficoTempo
// ---------------------------------------------------------------------------
TESTE(grafico_sem_dados_pede_para_aguardar) {
    const Historico historico;
    VERIFICAR(contem(sem_ansi(GraficoTempo{campo("altitude")}.quadro(historico)), "aguardando"));
}

TESTE(grafico_mostra_titulo_e_valor_atual) {
    Historico historico;
    for (int i = 0; i <= 10; ++i) {
        historico.registrar(estado_em(i, i));
    }
    const std::string quadro = sem_ansi(GraficoTempo{campo("altitude")}.quadro(historico));
    VERIFICAR(contem(quadro, "Altitude relativa (m)"));
    VERIFICAR(contem(quadro, "atual: 10.0 m"));
}

TESTE(grafico_liga_os_pontos) {
    // Dois pontos, um embaixo e outro em cima: soltos, ocupariam só 2 linhas;
    // ligados, as linhas com '*' formam um bloco contínuo.
    Historico historico;
    historico.registrar(estado_em(0, 0.0));
    historico.registrar(estado_em(1, 100.0));
    const int altura = 16;
    std::vector<int> com_marca;
    int indice = 0;
    for (const std::string& linha :
         linhas(sem_ansi(GraficoTempo{campo("altitude"), 70, altura}.quadro(historico)))) {
        const auto barra = linha.find(" |");
        if (barra == std::string::npos) {
            continue;
        }
        if (linha.find('*', barra) != std::string::npos) {
            com_marca.push_back(indice);
        }
        ++indice;
    }
    VERIFICAR(static_cast<int>(com_marca.size()) >= altura * 2 / 3);
    VERIFICAR(com_marca.back() - com_marca.front() + 1 == static_cast<int>(com_marca.size()));
}

TESTE(grafico_ignora_valores_desconhecidos) {
    Historico historico;
    EstadoVeiculo estado = estado_em(0);
    estado.bateria = Bateria{};  // NaN: ainda não chegou
    historico.registrar(estado);
    const std::string quadro = sem_ansi(GraficoTempo{campo("bateria")}.quadro(historico));
    VERIFICAR(contem(quadro, "aguardando") && !contem(quadro, "nan"));
}

TESTE(grafico_mostra_alarme_de_bateria) {
    Historico historico;
    EstadoVeiculo estado = estado_em(0, 5.0);
    estado.bateria.percentual = 10.0;
    historico.registrar(estado);
    VERIFICAR(contem(sem_ansi(GraficoTempo{campo("altitude")}.quadro(historico)), "BATERIA BAIXA"));
}

TESTE(catalogo_tem_os_campos_novos) {
    for (const char* nome : {"velocidade", "subida", "tensao", "satelites", "rumo", "distancia_home"}) {
        VERIFICAR(campos_disponiveis().count(nome) == 1);
    }
}

// ---------------------------------------------------------------------------
// HorizonteArtificial
// ---------------------------------------------------------------------------
TESTE(horizonte_sem_dados_pede_para_aguardar) {
    const Historico historico;
    VERIFICAR(contem(sem_ansi(HorizonteArtificial{}.quadro(historico)), "aguardando"));
}

TESTE(horizonte_nivelado_mostra_escada_arco_e_fita_de_rumo) {
    Historico historico;
    historico.registrar(estado_em(0));  // rolagem, arfagem e rumo zerados
    const std::string quadro = sem_ansi(HorizonteArtificial{}.quadro(historico));
    VERIFICAR(contem(quadro, "10 -------"));  // degrau de 10 graus da escada
    VERIFICAR(contem(quadro, "|"));           // marca de 0 grau do arco de rolagem
    VERIFICAR(contem(quadro, "^ 0"));         // ponteiro da fita de rumo
    VERIFICAR(contem(quadro, " N "));         // norte no centro da fita
    VERIFICAR(!contem(quadro, "ROLAGEM EXCESSIVA"));
}

TESTE(horizonte_avisa_rolagem_excessiva) {
    Historico historico;
    EstadoVeiculo estado = estado_em(0);
    estado.atitude.rolagem = 50.0;
    historico.registrar(estado);
    VERIFICAR(contem(sem_ansi(HorizonteArtificial{}.quadro(historico)), "ROLAGEM EXCESSIVA"));
}

TESTE(horizonte_tem_sempre_a_mesma_altura) {
    // O renderizador redesenha por cima do quadro anterior: se a altura
    // variasse, sobrariam linhas velhas na tela.
    Historico calmo;
    calmo.registrar(estado_em(0));
    Historico agitado;
    EstadoVeiculo estado = estado_em(0);
    estado.atitude = Atitude{60.0, -25.0, 275.0};
    estado.bateria.percentual = 5.0;
    agitado.registrar(estado);
    const HorizonteArtificial horizonte;
    VERIFICAR(linhas(horizonte.quadro(calmo)).size() == linhas(horizonte.quadro(agitado)).size());
}

}  // namespace

int main() {
    int falhas = 0;
    for (const auto& [nome, corpo] : casos()) {
        try {
            corpo();
            std::cout << "[ ok ] " << nome << '\n';
        } catch (const std::exception& erro) {
            ++falhas;
            std::cout << "[FALHA] " << nome << ": " << erro.what() << '\n';
        }
    }
    std::cout << '\n' << casos().size() - static_cast<std::size_t>(falhas) << " de "
              << casos().size() << " testes passaram\n";
    return falhas == 0 ? 0 : 1;
}
