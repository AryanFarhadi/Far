'use strict';

module.exports = {
  CompletionItemKind: {
    Text: 0, Method: 1, Function: 2, Constructor: 3, Field: 4,
    Variable: 5, Class: 6, Interface: 7, Module: 8, Property: 9,
    Enum: 10, Keyword: 11, Snippet: 14, TypeParameter: 25, Value: 12, Struct: 22,
  },
  SymbolKind: { Function: 11, Struct: 22, Class: 4, Enum: 9, Variable: 12 },
  DiagnosticSeverity: { Error: 0, Warning: 1 },
  CompletionItem: class {
    constructor(label, kind) { this.label = label; this.kind = kind; }
  },
  CompletionList: class {
    constructor(items, incomplete) { this.items = items; this.isIncomplete = incomplete; }
  },
  SnippetString: class {
    constructor(value) { this.value = value; }
  },
  MarkdownString: class {
    constructor(value) { this.value = value; }
    appendCodeblock(code) { this.value += `\n\`\`\`far\n${code}\n\`\`\``; }
  },
  SignatureInformation: class {
    constructor(label, doc) { this.label = label; this.documentation = doc; this.parameters = []; }
  },
  ParameterInformation: class { constructor(label) { this.label = label; } },
  SignatureHelp: class { constructor(sigs, active) { this.signatures = sigs; this.activeSignature = active; } },
  Hover: class { constructor(contents, range) { this.contents = contents; this.range = range; } },
  Location: class { constructor(uri, range) { this.uri = uri; this.range = range; } },
  Position: class { constructor(line, col) { this.line = line; this.character = col; } },
  Range: class { constructor(start, end) { this.start = start; this.end = end; } },
  TextEdit: { insert: (pos, text) => ({ pos, text }) },
  DocumentSymbol: class {
    constructor(name, detail, kind, range, sel) {
      this.name = name; this.detail = detail; this.kind = kind; this.range = range; this.selectionRange = sel;
    }
  },
};
