// Fábrica dos comandos que já vêm no esqueleto.
#pragma once

#include "cli/Comando.hpp"

namespace gcs {

/// Monta o registro com todos os comandos do console. As classes ficam em
/// ComandosPadrao.cpp; para criar um comando novo, copie uma delas e registre aqui.
RegistroDeComandos comandos_padrao();

}  // namespace gcs
