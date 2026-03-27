const httpServer = require('http-server');
const path = require('path');

const server = httpServer.createServer({
    root: path.join(__dirname, 'public'),
    cors: true,
    cache: -1  // disable caching for development
});

const PORT = 8080;
server.listen(PORT, '127.0.0.1', () => {
    console.log(`CrumbsBrowser TestSite running at http://localhost:${PORT}`);
});
