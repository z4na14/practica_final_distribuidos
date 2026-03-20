#import "@local/report-template-typst:0.1.0": conf, azuluc3m

#show: conf.with(
  degree: "Grado en Ingeniería Informática",
  subject: "Sistemas distribuidos",
  year: (25, 26),
  project: "Práctica 1",
  title: "Desarrollo de una aplicación concurrente",
  group: 81,
  authors: (
    (
      name: "Jorge Adrian",
      surname: "Saghin Dudulea",
      nia: 100522257
    ),
    (
      name: "Denis Loren",
      surname: "Moldovan",
      nia: 100522240
    ),
  ),
  professor: "Felix García",
  toc: true,
  logo: "new",
  language: "es"
)

#set table(
      stroke: none,
      fill: (x, y) => if calc.even(y) == false { azuluc3m.transparentize(80%) },
      inset: (x: 1.0em, y: 0.5em),
      gutter: 0.2em, row-gutter: 0em, column-gutter: 0em
    )
#show table.cell.where(y: 0) : set text(weight: "bold")