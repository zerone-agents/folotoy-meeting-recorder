FROM node:22.13.0-bookworm-slim AS dependencies
WORKDIR /app
COPY package.json package-lock.json ./
COPY scripts/patch-agent-sdk.mjs ./scripts/
RUN npm ci --omit=dev

FROM node:22.13.0-bookworm-slim
ARG MEETING_VERSION
ENV MEETING_VERSION=${MEETING_VERSION}
ENV NODE_ENV=production MEETING_HOST=0.0.0.0 MEETING_DATA_DIR=/var/lib/meeting
WORKDIR /app
RUN mkdir -p /var/lib/meeting && chown node:node /var/lib/meeting && chmod 700 /var/lib/meeting
COPY --from=dependencies --chown=node:node /app/node_modules ./node_modules
COPY --chown=node:node package.json package-lock.json ./
COPY --chown=node:node src ./src
USER node
EXPOSE 3443
CMD ["node", "src/index.mjs"]
