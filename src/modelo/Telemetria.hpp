// Objetos de valor que representam a telemetria de um veículo.
//
// São structs simples e copiáveis: cada pacote de telemetria que chega gera um
// novo EstadoVeiculo completo, em vez de alterar campos espalhados pelo
// programa. Copiar um estado é baratíssimo e torna o acesso entre threads
// (a telemetria do MAVSDK chega em outra thread) muito mais simples de acertar.
#pragma once

#include <chrono>
#include <limits>
#include <string>

namespace gcs {

using Instante = std::chrono::steady_clock::time_point;

/// Orientação do veículo, em graus (convenção aeronáutica).
struct Atitude {
    double rolagem = 0.0;  // roll  — positivo com a asa direita abaixada
    double arfagem = 0.0;  // pitch — positivo com o nariz acima do horizonte
    double guinada = 0.0;  // yaw   — 0 = norte, cresce para leste
};

/// Posição global informada pelo GPS.
struct Posicao {
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude_absoluta_m = 0.0;  // em relação ao nível do mar (AMSL)
    double altitude_relativa_m = 0.0;  // em relação ao ponto de decolagem
};

/// Estado da bateria principal. Começa como NaN ("ainda não chegou"), para que
/// um 0 % inventado não dispare o alarme de bateria baixa nem apareça no gráfico.
struct Bateria {
    double tensao_v = std::numeric_limits<double>::quiet_NaN();
    double percentual = std::numeric_limits<double>::quiet_NaN();  // 0 a 100
};

/// Velocidades derivadas do vetor NED (norte, leste, baixo).
struct Velocidade {
    double solo_mps = 0.0;    // componente horizontal
    double subida_mps = 0.0;  // positiva subindo (o NED aponta para BAIXO)
};

/// Enlace de rádio-controle.
struct Radio {
    bool disponivel = false;
    double sinal_percentual = -1.0;  // 0 a 100; negativo quando desconhecido
};

/// Ponto de decolagem registrado pelo autopiloto (para onde o RTL volta).
struct Home {
    bool definida = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude_absoluta_m = 0.0;
};

/// Fotografia completa do veículo em um instante.
struct EstadoVeiculo {
    std::string modo_de_voo = "DESCONHECIDO";
    bool armado = false;
    bool em_voo = false;
    Atitude atitude{};
    Posicao posicao{};
    Bateria bateria{};
    Velocidade velocidade{};
    int satelites = 0;
    Radio radio{};
    Home home{};
    Instante instante = std::chrono::steady_clock::now();

    /// Distância horizontal até o ponto de decolagem; 0 se a home não for conhecida.
    double distancia_home_m() const;

    /// Algumas linhas legíveis, usadas pelo comando `status`.
    std::string resumo() const;
};

/// Segundos decorridos entre dois instantes.
double segundos_entre(Instante inicio, Instante fim);

/// Distância horizontal entre dois pontos (fórmula de haversine), em metros.
double distancia_horizontal_m(double latitude_a, double longitude_a, double latitude_b,
                              double longitude_b);

}  // namespace gcs
