import {readFileSync,writeFileSync} from 'node:fs';

// SDK 3.8.0 drops the OpenAI finish reason and can treat partial streams as success.
// Remove this patch when the pinned SDK provides equivalent completion checks.
const sdk=new URL('../node_modules/@zerone-agent/agent-sdk/',import.meta.url);
const {version}=JSON.parse(readFileSync(new URL('package.json',sdk),'utf8'));
if(version!=='3.8.0')throw new Error(`Recheck the streaming completion patch for agent-sdk ${version}`);

const patches=[
 ['dist/providers/openai.js',[
  ['        let currentBlockIndex = -1;',
   '        let currentBlockIndex = -1;\n        let openAIFinishReason = null;'],
  ['                const delta = choice.delta;',
   '                if (choice.finish_reason)\n                    openAIFinishReason = choice.finish_reason;\n                const delta = choice.delta;'],
  ["        yield { type: 'done', index: -1 };",
   "        yield { type: 'done', index: -1, openAIFinishReason };"],
 ]],
 ['dist/engine.js',[
  ['                    response = buildResponseFromChunks(chunks);',
   `                    const terminal = chunks.findLast(chunk => chunk.type === 'done');
                    if (terminal && Object.hasOwn(terminal, 'openAIFinishReason') &&
                        !['stop', 'tool_calls'].includes(terminal.openAIFinishReason))
                        streamTruncated = true;
                    response = buildResponseFromChunks(chunks);`],
  ['            // Handle max_output_tokens recovery',
   '            if (streamTruncated)\n                break;\n            // Handle max_output_tokens recovery'],
  ['        const endSubtype = budgetExceeded',
   "        const endSubtype = streamTruncated\n            ? 'error_incomplete_stream'\n            : budgetExceeded"],
 ]],
];

const outputs=patches.map(([path,edits])=>{
 const file=new URL(path,sdk);
 let source=readFileSync(file,'utf8');
 for(const [before,after] of edits){
  if(source.includes(after))continue;
  const start=source.indexOf(before);
  if(start===-1||start!==source.lastIndexOf(before))throw new Error(`Cannot patch agent-sdk file ${path}`);
  source=source.slice(0,start)+after+source.slice(start+before.length);
 }
 return [file,source];
});
for(const [file,source] of outputs)writeFileSync(file,source);
