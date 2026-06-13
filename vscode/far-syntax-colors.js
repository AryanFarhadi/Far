'use strict';

/** Far syntax palette */
const FAR_ORANGE = '#FF9F43';       // methods, builtins, IO
const FAR_LIGHT_BLUE = '#9CDCFE';   // variables, params, members
const FAR_LIGHT_PURPLE = '#C792EA'; // keywords, return, control flow
const FAR_SKY_BLUE = '#7DD3FC';     // types
const FAR_PEACH = '#F4BF9A';        // strings
const FAR_MINT = '#A8E6A0';         // numbers
const FAR_SAGE = '#7A8B7E';         // comments
const FAR_LAVENDER = '#D4BFFF';    // brackets, punctuation
const FAR_SOFT_PURPLE = '#B794F6'; // operators

/** Far-only TextMate color rules (source.far prefix). */
const FAR_TEXTMATE_RULES = [
  // Comments
  { scope: 'source.far comment.line.number-sign.far', settings: { foreground: FAR_SAGE, fontStyle: 'italic' } },

  // Strings
  { scope: 'source.far string.quoted.double.far, source.far string.quoted.single.far, source.far string.interpolated.far', settings: { foreground: FAR_PEACH } },

  // Numbers
  { scope: 'source.far constant.numeric.far, source.far constant.numeric.hex.far, source.far constant.numeric.binary.far', settings: { foreground: FAR_MINT } },
  { scope: 'source.far constant.language.far', settings: { foreground: FAR_LIGHT_PURPLE } },

  // Keywords — light purple (return, if, while, fun, import, ...)
  { scope: 'source.far keyword.control.far', settings: { foreground: FAR_LIGHT_PURPLE } },
  { scope: 'source.far keyword.control.import.far', settings: { foreground: FAR_LIGHT_PURPLE } },
  { scope: 'source.far keyword.declaration.far', settings: { foreground: FAR_LIGHT_PURPLE } },
  { scope: 'source.far storage.modifier.far', settings: { foreground: FAR_SOFT_PURPLE } },

  // Types — sky blue
  { scope: 'source.far storage.type.far', settings: { foreground: FAR_SKY_BLUE } },
  { scope: 'source.far entity.name.type.far', settings: { foreground: FAR_SKY_BLUE } },

  // Methods / functions — orange
  { scope: 'source.far support.function.io.far', settings: { foreground: FAR_ORANGE } },
  { scope: 'source.far support.function.builtin.far', settings: { foreground: FAR_ORANGE } },
  { scope: 'source.far entity.name.function.far, source.far entity.name.function.call.far, source.far entity.name.function.member.far', settings: { foreground: FAR_ORANGE } },

  // Variables — light blue
  { scope: 'source.far variable.other.readwrite.far, source.far variable.other.assignment.far, source.far variable.other.index.far, source.far variable.other.slice.far', settings: { foreground: FAR_LIGHT_BLUE } },
  { scope: 'source.far variable.parameter.far', settings: { foreground: FAR_LIGHT_BLUE, fontStyle: 'italic' } },
  { scope: 'source.far variable.other.member.far', settings: { foreground: FAR_LIGHT_BLUE } },
  { scope: 'source.far variable.other.import.far', settings: { foreground: FAR_LIGHT_BLUE } },

  // Imports / namespaces
  { scope: 'source.far entity.name.namespace.far', settings: { foreground: FAR_SKY_BLUE } },

  // Operators
  { scope: 'source.far keyword.operator.slice.far, source.far keyword.operator.range.far', settings: { foreground: FAR_ORANGE } },
  { scope: 'source.far keyword.operator.arrow.far', settings: { foreground: FAR_LIGHT_PURPLE } },
  { scope: 'source.far keyword.operator.comparison.far, source.far keyword.operator.logical.far, source.far keyword.operator.bitwise.far', settings: { foreground: FAR_SOFT_PURPLE } },
  { scope: 'source.far keyword.operator.assignment.far, source.far keyword.operator.far', settings: { foreground: FAR_LAVENDER } },

  // Punctuation
  { scope: 'source.far punctuation.section.slice.begin.far, source.far punctuation.section.slice.end.far, source.far punctuation.section.index.begin.far, source.far punctuation.section.index.end.far', settings: { foreground: FAR_LAVENDER } },
  { scope: 'source.far punctuation.definition.block.far, source.far punctuation.definition.parameters.far', settings: { foreground: FAR_LAVENDER } },
  { scope: 'source.far punctuation.separator.far, source.far punctuation.accessor.far', settings: { foreground: '#A0A0B0' } },

  // Attributes
  { scope: 'source.far entity.other.attribute-name.far', settings: { foreground: '#F6D365' } },
];

module.exports = {
  FAR_BRAND_ORANGE: FAR_ORANGE,
  FAR_ORANGE,
  FAR_LIGHT_BLUE,
  FAR_LIGHT_PURPLE,
  FAR_TEXTMATE_RULES,
};
