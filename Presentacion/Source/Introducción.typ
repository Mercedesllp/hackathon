#import "@preview/touying:0.6.1": *
#import themes.metropolis: *

#slide(title: "¿Cuál es nuestra idea?")[
  Imaginate un juego de la viborita/Snake, pero con *esteroides*. #pause
  #align(center + horizon)[
    #box(
      image("assets/vivora_esteroides.png"),
      radius: 0.25em,
      clip: true,
    )
  ]
]

#slide(title: "Recordemos cómo se juega Snake")[
  #grid(
    columns: (1fr, 1fr),
    column-gutter: 1.5em,
    align: horizon,

    [
      #set text(size: 1.1em)
      #set par(justify: false, leading: 0.9em)

      En un tablero *cerrado*, el objetivo es recolectar fruta que aparece de manera aleatoria.

      - Cada fruta te da *puntos*.
      - Además, te hace *crecer la cola*, lo que aumenta la dificultad.
      - El juego termina si chocás tu cabeza contra tu cuerpo o contra una *pared*.
      - Simple, fácil, *solitario y aburrido…*
    ],

    figure(
      image("assets/snake_juego.png", width: 100%, height: 70%, fit: "contain"),
    ),
  )
]

#slide(title: "¿Cómo que tiene esteroides?")[
  Nuestra idea es que sea *cooperativo*:

  - *Distribuido*: sin servidor central.
  - *Liviano*: corre en cualquier dispositivo.
  - *Privado*: sin que ninguna corpo te robe los datos ;)
]

#slide(title: "¿Cómo se juega? Quiero jugar YA")[
  Supongamos dos jugadoras: *Amanda* y *Beatriz*.

  - *Amanda* juega al Snake de toda la vida.
  - *Beatriz* resuelve un *puzzle*.

  #align(center)[
    #image("assets/pantallas_juego.png", width: 80%)
  ]
]

#slide(title: "¿Cómo se juega? (2/3)")[
  - Si Beatriz resuelve el puzzle, puede *ponerle un obstáculo/dificultad* a Amanda en su partida.

  #align(center)[
    #image("assets/pantallas_obstaculo.png", width: 90%)
  ]
]

#slide(title: "¿Cómo se juega? (3/3)")[
  Cuando Amanda se come *dos manzanas*, tiene dos opciones:

  - *Pasar a la pantalla de Beatriz* con la víbora y *cambiar de roles*.
  - *Seguir agrandando la víbora*, para dejársela más difícil a Beatriz cuando se la pase, pero mientras tanto, esta puede tirarle *más bloques*.

  #align(center)[
    #image("assets/opciones_dos_manzanas.png", width: 80%)
  ]
]

#slide(title: "¿Cómo termina? Quiero ganar YA")[
  Es muy simple, mi querido saltamontes: quien se choca con un bloque, con su propio cuerpo o con una pared, *pierde* :( #pause

  Snake ya *pasó de moda*: ahora les jóvenes juegan *Esneik*.
]

