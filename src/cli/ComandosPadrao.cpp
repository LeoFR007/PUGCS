#include "cli/ComandosPadrao.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "EstacaoDeControle.hpp"
#include "modelo/Veiculo.hpp"
#include "visual/GraficoTempo.hpp"
#include "visual/HorizonteArtificial.hpp"

namespace gcs {
namespace {

/// Converte o texto INTEIRO em número finito: "abc", "10abc", "nan" e "inf"
/// são recusados com ErroDeComando.
double ler_numero(const std::string& texto, const std::string& uso) {
    std::size_t lidos = 0;
    double valor = 0.0;
    try {
        valor = std::stod(texto, &lidos);
    } catch (const std::logic_error&) {  // invalid_argument e out_of_range
        lidos = 0;
    }
    if (lidos == 0 || lidos != texto.size() || !std::isfinite(valor)) {
        throw ErroDeComando("'" + texto + "' não é um número válido. uso: " + uso);
    }
    return valor;
}

// ---------------------------------------------------------------------------
// Comandos de exemplo. Cada um é uma classe pequena; o console não sabe quais
// existem, só pede ao registro o comando com o nome digitado.
// ---------------------------------------------------------------------------

class Ajuda : public Comando {
public:
    std::string nome() const override { return "ajuda"; }
    std::string ajuda() const override { return "lista os comandos disponíveis"; }

    void executar(EstacaoDeControle& estacao, const std::vector<std::string>&) override {
        std::cout << "Comandos:\n";
        for (const Comando* comando : estacao.comandos().todos()) {
            std::cout << "  " << comando->linha_de_ajuda() << '\n';
        }
    }
};

class Status : public Comando {
public:
    std::string nome() const override { return "status"; }
    std::string ajuda() const override { return "mostra o modo de voo e a telemetria atual"; }

    void executar(EstacaoDeControle& estacao, const std::vector<std::string>&) override {
        std::cout << estacao.veiculo() << '\n';
    }
};

class Modo : public Comando {
public:
    std::string nome() const override { return "modo"; }
    std::string uso() const override { return "modo <NOME>"; }
    std::string ajuda() const override { return "troca o modo de voo (ex.: modo RTL)"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (argumentos.size() != 1) {
            throw ErroDeComando("uso: " + uso());
        }
        estacao.veiculo().alterar_modo_de_voo(argumentos.front());
        std::cout << "pedido de troca de modo enviado\n";
    }
};

class Plotar : public Comando {
public:
    std::string nome() const override { return "plotar"; }
    std::string uso() const override { return "plotar <campo>"; }
    std::string ajuda() const override {
        return "abre o gráfico de um campo: " + nomes_dos_campos();
    }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (argumentos.size() != 1) {
            throw ErroDeComando("uso: " + uso());
        }
        const auto& catalogo = campos_disponiveis();
        const auto encontrado = catalogo.find(argumentos.front());
        if (encontrado == catalogo.end()) {
            throw ErroDeComando("campo '" + argumentos.front() +
                                "' não existe. Disponíveis: " + nomes_dos_campos());
        }
        const GraficoTempo grafico{encontrado->second};
        estacao.exibir_painel(grafico);
    }
};

class Horizonte : public Comando {
public:
    std::string nome() const override { return "horizonte"; }
    std::string ajuda() const override { return "abre o horizonte artificial"; }

    void executar(EstacaoDeControle& estacao, const std::vector<std::string>&) override {
        const HorizonteArtificial horizonte;
        estacao.exibir_painel(horizonte);
    }
};

class Armar : public Comando {
public:
    std::string nome() const override { return "armar"; }
    std::string ajuda() const override { return "arma os motores"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (!argumentos.empty()) {
            throw ErroDeComando("uso: " + uso());
        }
        estacao.veiculo().armar();
        std::cout << "veículo armado\n";
    }
};

class Desarmar : public Comando {
public:
    std::string nome() const override { return "desarmar"; }
    std::string ajuda() const override { return "desarma os motores (só no chão)"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (!argumentos.empty()) {
            throw ErroDeComando("uso: " + uso());
        }
        estacao.veiculo().desarmar();
        std::cout << "veículo desarmado\n";
    }
};

class Decolar : public Comando {
public:
    std::string nome() const override { return "decolar"; }
    std::string uso() const override { return "decolar <altura_m>"; }
    std::string ajuda() const override { return "decola até a altura pedida (precisa estar armado)"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (argumentos.size() != 1) {
            throw ErroDeComando("uso: " + uso());
        }
        const double altura = ler_numero(argumentos.front(), uso());
        if (altura <= 0.0) {
            throw ErroDeComando("a altura precisa ser maior que zero");
        }
        estacao.veiculo().decolar(altura);
        std::cout << "decolagem para " << altura << " m enviada\n";
    }
};

class Pousar : public Comando {
public:
    std::string nome() const override { return "pousar"; }
    std::string ajuda() const override { return "pousa no ponto atual"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (!argumentos.empty()) {
            throw ErroDeComando("uso: " + uso());
        }
        estacao.veiculo().pousar();
        std::cout << "pouso enviado\n";
    }
};

class Rtl : public Comando {
public:
    std::string nome() const override { return "rtl"; }
    std::string ajuda() const override { return "volta para a home e pousa"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (!argumentos.empty()) {
            throw ErroDeComando("uso: " + uso());
        }
        estacao.veiculo().retornar_para_casa();
        std::cout << "retorno para casa enviado\n";
    }
};

class IrPara : public Comando {
public:
    std::string nome() const override { return "ir_para"; }
    std::string uso() const override { return "ir_para <lat> <lon> <alt_m>"; }
    std::string ajuda() const override { return "voa até a coordenada (alt. relativa à home)"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (argumentos.size() != 3) {
            throw ErroDeComando("uso: " + uso());
        }
        // Os limites (latitude, longitude, altitude > 0) o Veiculo confere.
        estacao.veiculo().ir_para(ler_numero(argumentos[0], uso()),
                                  ler_numero(argumentos[1], uso()),
                                  ler_numero(argumentos[2], uso()));
        std::cout << "deslocamento enviado\n";
    }
};

class Gravar : public Comando {
public:
    std::string nome() const override { return "gravar"; }
    std::string uso() const override { return "gravar <arquivo.csv|.json>"; }
    std::string ajuda() const override { return "exporta o histórico para analisar depois"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (argumentos.size() != 1) {
            throw ErroDeComando("uso: " + uso());
        }
        const std::string& caminho = argumentos.front();
        const auto termina_com = [&caminho](const std::string& fim) {
            return caminho.size() > fim.size() &&
                   caminho.compare(caminho.size() - fim.size(), fim.size(), fim) == 0;
        };
        if (termina_com(".csv")) {
            estacao.historico().exportar_csv(caminho);
        } else if (termina_com(".json")) {
            estacao.historico().exportar_json(caminho);
        } else {
            throw ErroDeComando("use um arquivo .csv ou .json");
        }
        std::cout << estacao.historico().tamanho() << " amostras gravadas em " << caminho << '\n';
    }
};

class Limpar : public Comando {
public:
    std::string nome() const override { return "limpar"; }
    std::string ajuda() const override { return "apaga o histórico (os gráficos recomeçam)"; }

    void executar(EstacaoDeControle& estacao,
                  const std::vector<std::string>& argumentos) override {
        if (!argumentos.empty()) {
            throw ErroDeComando("uso: " + uso());
        }
        estacao.historico().limpar();
        std::cout << "histórico apagado\n";
    }
};

class Sair : public Comando {
public:
    std::string nome() const override { return "sair"; }
    std::string ajuda() const override { return "encerra a estação de controle"; }

    void executar(EstacaoDeControle& estacao, const std::vector<std::string>&) override {
        estacao.encerrar();
    }
};

}  // namespace

RegistroDeComandos comandos_padrao() {
    RegistroDeComandos registro;
    registro.registrar(std::make_unique<Ajuda>());
    registro.registrar(std::make_unique<Status>());
    registro.registrar(std::make_unique<Modo>());
    registro.registrar(std::make_unique<Plotar>());
    registro.registrar(std::make_unique<Horizonte>());
    registro.registrar(std::make_unique<Armar>());
    registro.registrar(std::make_unique<Desarmar>());
    registro.registrar(std::make_unique<Decolar>());
    registro.registrar(std::make_unique<Pousar>());
    registro.registrar(std::make_unique<Rtl>());
    registro.registrar(std::make_unique<IrPara>());
    registro.registrar(std::make_unique<Gravar>());
    registro.registrar(std::make_unique<Limpar>());
    registro.registrar(std::make_unique<Sair>());
    return registro;
}

}  // namespace gcs
