#import "@preview/blinky:0.2.0": link-bib-urls
#import "@preview/touying:0.6.1": *
#import themes.metropolis: *

#show: metropolis-theme.with(
  aspect-ratio: "16-9",
  align: horizon,
  // config-common(handout: true), // Descomentar para ignorar/sacar pausas.
  config-colors(
    primary: rgb("#008296"),
    primary-light: rgb("#A9DADE"),
    secondary: rgb("#011627"),
    neutral-lightest: rgb("#fafafa"),
    neutral-dark: rgb("#1D1D1B"),
    neutral-darkest: rgb("#1D1D1B"),
  ),
  config-info(
    title: [#text(weight: 600)[Esneik:  El juego que necesitas en tu vida.]],
    subtitle: [],
    author: [*Mercedes* Llanos Pontaut \
    *Manuel* Romero \
    *Simón* Pedro Atías Abregú \
    *Esteban* Vivanco]
  ),
)

#title-slide()
#include "Source/Introducción.typ"
#include "Source/Implementación.typ"
#include "Source/Futuro.typ"
#include "Source/Source.typ"
#include "Source/Fin.typ"
